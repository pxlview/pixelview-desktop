"""Compile the production DeckLink discovery with offline device fixtures."""
import unittest
from test_first_launch_defaults import ROOT, compiled


class CaptureInputDiscovery(unittest.TestCase):
    def test_input_properties_exclude_output_only_without_changing_output_list(self):
        source = (ROOT/'plugins/decklink/decklink-source.cpp').read_text()
        call = next(line.strip() for line in source.splitlines() if 'fill_out_devices(list' in line)
        implementation = (ROOT/'plugins/decklink/decklink-devices.cpp').read_text()
        implementation = implementation[implementation.index('void fill_out_devices'):]
        declaration = (ROOT/'plugins/decklink/decklink-devices.hpp').read_text().splitlines()[-1]
        compiled(r'''
#include <cassert>
#include <string>
#include <vector>
struct DeckLinkDevice {
 std::string name; std::vector<int> modes;
 const std::string &GetDisplayName(){return name;}
 const std::string &GetHash(){return name;}
 const std::vector<int> &GetInputModes(){return modes;}
};
struct Discovery {
 std::vector<DeckLinkDevice*> devices; int locks=0;
 void Lock(){++locks;} void Unlock(){--locks;}
 const auto &GetDevices(){return devices;}
} discovery, *deviceEnum=&discovery;
using obs_property_t=std::vector<std::string>;
void obs_property_list_add_string(obs_property_t *list,const char*,const char *id){list->push_back(id);}
DECLARATION
IMPLEMENTATION
void inputProperties(obs_property_t *list){INPUT_CALL}
int main(){
 DeckLinkDevice monitor{"Mini Monitor 3G",{}}, capture{"4K Mini",{1}}, second{"Other capture",{1}};
 discovery.devices={&monitor,&capture,&second};
 obs_property_t inputs; inputProperties(&inputs);
 assert(inputs.size()==2 && inputs.front()=="4K Mini");
 obs_property_t outputs; fill_out_devices(&outputs);
 assert(outputs.size()==3 && outputs.front()=="Mini Monitor 3G");
 assert(discovery.locks==0);
}
'''.replace('DECLARATION', declaration).replace('IMPLEMENTATION', implementation).replace('INPUT_CALL', call))


if __name__ == '__main__':
    unittest.main()
