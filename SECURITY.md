# Pixelview Desktop security policy

## Reporting a vulnerability

Use GitHub's private vulnerability reporting for this repository:

1. Open the repository's **Security** tab.
2. Open **Advisories**.
3. Select **Report a vulnerability**.

Do not put exploit details, credentials, or user data in a public issue. If private vulnerability reporting is unavailable, contact Pixelview through <https://pixelview.io> and ask for a private security channel before sharing details.

For non-sensitive defects, use the public GitHub issue tracker.

## Scope

Reports concerning Pixelview Desktop's updater, release artifacts, local secret handling, streaming control plane, capture pipeline, or modified OBS-derived code are in scope. Dependency vulnerabilities should also be reported to the dependency's maintainer when appropriate.

Pixelview Desktop is derived from OBS Studio but is independently maintained. Do not send Pixelview-specific vulnerabilities or Pixelview credentials to the OBS Project. A vulnerability that also affects unmodified OBS Studio may be reported separately through OBS's own security process.

## Research guidelines

- Test only systems and data you own or are explicitly authorized to use.
- Avoid privacy violations, destructive actions, and service disruption.
- Provide reproducible steps and a minimal proof of concept.
- Keep the report confidential until a coordinated disclosure date is agreed.
- Never include live credentials or personal data in the report.

Pixelview does not currently operate a bug-bounty program.
