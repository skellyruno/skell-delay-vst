# Legal and naming – decisions for you

I am not a lawyer; these are the things to settle, with templates to start from. Have a lawyer read the EULA before you sell.

## 1. Names and codes (cannot be changed after release without breaking saved projects)
- [ ] Company / publisher name -> `COMPANY_NAME` in `CMakeLists.txt` (now "Skelly Audio"; confirm this is the legal/trading name you want shown in hosts). Also used in the installers and the macOS bundle id (`com.skellyaudio.skelldelay`).
- [ ] 4-character manufacturer code -> `PLUGIN_MANUFACTURER_CODE` (now `Skly`, chosen for "Skelly"; check it against Steinberg's current VST3 manufacturer-code guidance and existing vendor codes).
- [ ] Plugin code (currently `Dlay`) must be unique among *your* plugins. Keep the manufacturer/plugin identity codes stable after the first release so existing host projects and plugin identities remain compatible.
- [ ] Product name "Skell-Delay" by Skelly Audio (set in `CMakeLists.txt`): search the trademark registers (USPTO TESS/ EUIPO / WIPO Global Brand Database, class 9 software and class 41/9 audio) and a plain web/store search (KVR, Plugin Boutique, Gumroad, Splice) for conflicts before you print or advertise it.

## 2. JUCE licence (what you build with)
Check the current terms and tiers at https://juce.com/pricing before you ship. As I understand JUCE 8: free tiers exist for small revenue; closed-source commercial products above that need a paid licence; otherwise AGPLv3 would apply to the whole plugin. Do not rely on this summary.
- [ ] Pick the tier that matches your expected revenue and record the decision and evidence in RELEASE_APPROVAL.json; the package notice is rendered from that record.
- [ ] Check whether your tier requires the "Made with JUCE" splash (not present in this project today).

## 3. Plugin format licences
- [ ] VST3: the current product target is Windows x64 VST3 only. Check the VST3 SDK licence with Steinberg; "VST" is a Steinberg trademark, so use the logo/wording only as their guidelines allow.
- [ ] AU/macOS: not built or shipped by the current product target. The old macOS scripts remain archived in the repository but are not part of the supported release workflow.
- [ ] AAX (Pro Tools) is not built; it needs a separate Avid developer agreement and PACE signing.

## 4. Assets
- [ ] Artwork (`Source/Assets/skell_bg.png`): confirm you own it or hold a licence that allows commercial redistribution. I do not know where it came from.
- [ ] Fonts: the interface uses the system default font; nothing is embedded.
- [ ] Demo audio in `demos/` was rendered from a synthetic generator (no samples, no recorded voices).

## 5. Selling
- [ ] EULA: start from `EULA.txt` (a plain-English template, not reviewed).
- [ ] Refund policy, privacy policy (the plugin collects nothing and makes no network calls today), VAT/sales tax via a merchant of record (Paddle, FastSpring, Gumroad, Plugin Boutique) is the easy way to avoid doing it yourself.
- [ ] Licence keys / activation: optional. Decide before beta so testers see the real flow.


## 6. Fail-closed release approval record

The owner-controlled RELEASE_APPROVAL.json is intentionally committed as NOT_APPROVED.
Do not change it to APPROVED unless the named approver has reviewed and recorded evidence
for every field.

The signed version-tag workflow refuses to stage a signed release candidate unless the
record approves the selected JUCE license mode and evidence, VST3 SDK terms, the compiled
third-party dependency inventory, commercial artwork rights, a final reviewed EULA with
no template placeholders, and publisher/product trademark clearance. AGPLv3 selection
also requires a source-distribution reference.

Private preview packages include the pinned JUCE and VST3 SDK license texts plus a rendered
notice, but are explicitly not approved for redistribution and omit the draft EULA.
The gate is a release control, not legal advice or a substitute for qualified review.
