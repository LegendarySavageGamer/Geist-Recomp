# Geist report service

Receives reports from the Geist launcher and delivers them to Discord.
The webhook belongs in the server environment. The client uses a public HTTPS URL.

## Geist community deployment

The community client is configured for
https://heroes-of-twilight.duckdns.org/geist-reports/v1/reports.
The service is deployed behind the existing HTTPS proxy. Health is available at
https://heroes-of-twilight.duckdns.org/geist-reports/healthz.
An end-to-end launcher upload on November 19, 2026 delivered a PNG and a playable
H.264 MP4 to Discord; confirmed report ID: 4E8E66132E63.

The service has no public Docker port. The existing Caddy proxy and the service
share the private geist-reporting network, with proxy 172.30.111.2 and service
172.30.111.3. The service trusts only that proxy address. The existing proxy's
Compose override includes the external network, so proxy recreation preserves
the connection. Existing game-relay ports and routes remain unchanged.

The private server environment file contains the webhook and must never be
included in client/source archives. The public reporting.ini contains only the
HTTPS service URL. No additional hosting account is required for this deployment.

For another installation, use the standalone deployment below. Do not start its
second proxy on a machine already using ports 80/443; add the report route to the
existing proxy instead.

## Docker deployment

1. Point a hostname you own at the server.
2. Copy .env.example to .env and set REPORT_DOMAIN and GEIST_DISCORD_WEBHOOK.
3. Keep .env on the server and exclude it from release archives.
4. Run docker compose up --build -d.
5. Check https://YOUR_HOST/healthz for a JSON response with status equal to ok.
6. Create reporting.ini beside GeistLauncher.exe containing this line:

       endpoint=https://YOUR_HOST/v1/reports

7. Submit a clearly labeled test report from the launcher with a screenshot and
   a short MP4. Verify the report ID, complete text and both files in Discord.
   A healthy server alone does not prove that Discord delivery works.

Linux x64 and Windows x64 binaries are included. Docker supplies certificate roots
for outgoing Discord HTTPS, and Caddy handles incoming HTTPS.

For Windows, run the Windows executable with GEIST_DISCORD_WEBHOOK set. Its default
listen address is 127.0.0.1:8080. Put an HTTPS reverse proxy in front of it. If using
a private proxy, configure GEIST_TRUSTED_PROXY with that proxy address as a CIDR.
The proxy must overwrite X-Forwarded-For with the actual client address.

## API

POST /v1/reports with multipart/form-data.
The report field contains JSON with version, category, description, activity,
steps, expected, actual, comments, evidence_link and hardware.
The file field can be repeated for evidence attachments.

Version, category, description, activity, steps, expected and actual are required.
Hardware is optional and should only be collected with player consent.
Attach at most four PNG, JPEG or MP4 files totaling at most 8 MiB.
For larger videos, supply an HTTPS evidence_link. Links are never fetched by this service.
Images are checked by decoding their headers. MP4 files require an MP4 container signature.
Text fields are limited to 4000 UTF-8 bytes each.

Reports have random IDs. The complete text and every accepted attachment are sent
together. Discord mentions are disabled. Success requires Discord confirmation of
the created message and all attachments.

A successful response is HTTP 201 with delivered=true, report_id, and attachments.
A failed response must leave the client form available for retry. A timed-out
request may already have reached Discord, and the error explains that uncertainty.

## Operation

Limits are five attempts per IP per hour, 100 globally per hour, and four concurrent
submissions. These in-memory limits reset when the service restarts. Hosting-level
traffic protection should also be configured at the public edge.

Logs contain report IDs and evidence counts. The webhook and report contents are
excluded from logs. Attachments are processed in bounded memory.

Automated tests cover valid evidence, malformed fields, disguised files, upload
limits, rate limits, Discord failures and attachment confirmation.
Real Discord delivery must be verified after deployment.
