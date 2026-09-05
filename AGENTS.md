# Pixelview Desktop

Pixelview Desktop is an OBS-derived desktop application built specifically for the [Pixelview.io](https://pixelview.io) streaming platform. It keeps OBS’s proven capture, audio, encoding, and output foundations while replacing the general-purpose broadcaster workflow with a focused Pixelview experience.

## Product direction

- Present a simplified, operator-friendly interface for Pixelview capture and streaming.
- Detect supported capture hardware automatically and keep essential controls close to the preview.
- Apply reliable Pixelview-oriented video, audio, and encoding defaults instead of exposing unnecessary OBS complexity.
- Preserve access to the native OBS capabilities that are needed for safe operation: capture-device settings, output controls, statistics, errors, and licensing.
- Maintain separate Pixelview configuration and application identity so it does not overwrite a user’s stock OBS setup.

## Current first draft

This first draft includes the simplified interface, Blackmagic/DeckLink-oriented capture workflow, native preview framing, audio meters and monitoring controls, fixed HD/FPS and encoding preferences, and native OBS start/stop streaming, status, and detailed statistics.

It is intentionally not yet connected to Pixelview accounts or remote control services. Pixelview destination provisioning, WebSocket/HTTP authentication, backend integration, and remote playout/control remain future work. No server endpoint, credential, or account flow should be invented in this repository.

## Repository status discipline

Update this file in every commit that changes Pixelview Desktop’s delivered features, verification status, or known limitations. Keep the **Current first draft** section accurate: state what has been added, what has been verified, and what remains intentionally out of scope. Do not present planned Pixelview backend, authentication, or remote-control work as implemented.

## Upstream and licensing

This is a fork of OBS Studio and remains an OBS-derived GPL project. Preserve upstream notices, licensing information, and the source/build materials required for corresponding-source distribution. Product-specific changes should remain clearly documented and should not remove upstream attribution.

See `PIXELVIEW.md` and `docs/` for implementation, build, verification, and distribution details.
