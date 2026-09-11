#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Counterexample tests, not a receiver estimator or a shipping admission test.
Run run-obs-vt-cadence.py first to compile actual WHIP conversion + libdatachannel.
"""
import importlib.util
import json
import subprocess
import unittest
from fractions import Fraction
from pathlib import Path

spec=importlib.util.spec_from_file_location('audit',Path(__file__).with_name('run-obs-vt-cadence.py'))
assert spec and spec.loader
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)

class CadenceEvidence(unittest.TestCase):
    def convert(self,deltas):
        text=subprocess.run([str(audit.OUT/'whip-conversion')],input=''.join(f'{d}\n' for d in deltas),text=True,capture_output=True,check=True,timeout=10,env=audit.ENV).stdout
        return [int(row.split(',')[0]) for row in text.splitlines()]

    def test_common_rates_are_distinguishable(self):
        self.assertEqual(self.convert([33333,33334]),[3000,3000])
        self.assertEqual(self.convert([33366,33367]),[3003,3003])

    def test_distinct_legitimate_rationals_have_identical_rtp(self):
        # Every adjacent difference of floor(n*T) is floor(T) or ceil(T).
        # Exhaust those possibilities, rather than extrapolating a short run.
        intervals=[]
        for rate in [Fraction(30),Fraction(29999,1000)]:
            period=1000000/rate; floor=period.numerator//period.denominator
            intervals.extend([floor,floor+1])
        self.assertEqual(intervals,[33333,33334,33334,33335])
        self.assertEqual(self.convert(intervals),[3000]*4)
        self.assertNotEqual(Fraction(30),Fraction(29999,1000))
        self.assertLess(Fraction(29999,1000),30)

    def test_thirty_ceiling_cannot_be_proven_from_3000_ticks(self):
        period=1000000/Fraction(30001,1000)
        floor=period.numerator//period.denominator
        self.assertEqual(self.convert([floor,floor+1]),[3000,3000])
        self.assertGreater(Fraction(30001,1000),30)

    def test_ntsc_rational_also_has_an_alias(self):
        for rate in [Fraction(30000,1001),Fraction(29999,1001)]:
            period=1000000/rate; floor=period.numerator//period.denominator
            self.assertEqual(self.convert([floor,floor+1]),[3003,3003])

    def test_counterexample_is_present_in_actual_encoder_evidence(self):
        result=json.loads((audit.OUT/'result.json').read_text())
        rates={(r['fps_num'],r['fps_den']):r for r in result['rates']}
        for key in [(30,1),(29999,1000)]:
            self.assertEqual(set(rates[key]['rtp_increments']),{'3000'})
            self.assertEqual(rates[key]['header_fields']['vui_timing_info_present_flag'],0)
            self.assertEqual(rates[key]['header_fields']['vps_timing_info_present_flag'],0)
            self.assertEqual(rates[key]['distinct_decoded_frames'],rates[key]['packets'])

    def test_loss_wrap_jitter_cannot_disambiguate_identical_media_clocks(self):
        # The same impairment on identical media-clock traces still leaves
        # identical observations. This is NOT loss recovery implementation.
        deltas=self.convert([33333,33334,33334,33335])
        traces=[]
        for rate_increment in [deltas[0],deltas[-1]]:
            trace=[((2**32-6000+i*rate_increment)%2**32) for i in range(120)]
            observed=[(i,t,(i*33333333)+(i%7)*1000000) for i,t in enumerate(trace) if i%11!=5]
            # Equal outlier/discontinuity and identical arrival jitter.
            observed[50]=(observed[50][0],123,observed[50][2])
            traces.append(observed)
        self.assertEqual(traces[0],traces[1])

if __name__=='__main__':unittest.main()
