# Legal and naming – decisions for you

I am not a lawyer; these are the things to settle, with templates to start from. Have a lawyer read the EULA before you sell.

## 1. Names and codes (cannot be changed after release without breaking saved projects)
- [ ] Company / publisher name -> `COMPANY_NAME` in `CMakeLists.txt` (now "Skelly Audio"; confirm this is the legal/trading name you want shown in hosts). Also used in the installers and the macOS bundle id (`com.skellyaudio.skelldelay`).
- [ ] 4-character manufacturer code -> `PLUGIN_MANUFACTURER_CODE` (now `Skly`, chosen for "Skelly"; check it is not already used by another Audio Unit maker). Apple wants at least one upper-case letter; register it with Apple's list so it does not collide.
- [ ] Plugin code (currently `Dlay`) must be unique among *your* plugins; AU hosts identify the plugin by manufacturer + plugin code.
- [ ] Product name "Skell-Delay" by Skelly Audio (set in `CMakeLists.txt`): search the trademark registers (USPTO TESS/ EUIPO / WIPO Global Brand Database, class 9 software and class 41/9 audio) and a plain web/store search (KVR, Plugin Boutique, Gumroad, Splice) for conflicts before you print or advertise it.

## 2. JUCE licence (what you build with)
Check the current terms and tiers at https://juce.com/pricing before you ship. As I understand JUCE 8: free tiers exist for small revenue; closed-source commercial products above that need a paid licence; otherwise AGPLv3 would apply to the whole plugin. Do not rely on this summary.
- [ ] Pick the tier that matches your expected revenue and write it in `THIRD_PARTY_NOTICES.txt`.
- [ ] Check whether your tier requires the "Made with JUCE" splash (not present in this project today).

## 3. Plugin format licences
- [ ] VST3: the VST3 SDK licence (check the current terms with Steinberg); "VST" is a Steinberg trademark: use the logo/wording only as their guidelines allow.
- [ ] AU: Apple's Audio Unit terms are included in the developer agreement.
- [ ] AAX (Pro Tools) is not built; it needs a separate Avid developer agreement and PACE signing.

## 4. Assets
- [ ] Artwork (`Source/Assets/skell_bg.png`): confirm you own it or hold a licence that allows commercial redistribution. I do not know where it came from.
- [ ] Fonts: the interface uses the system default font; nothing is embedded.
- [ ] Demo audio in `demos/` was rendered from a synthetic generator (no samples, no recorded voices).

## 5. Selling
- [ ] EULA: start from `EULA.txt` (a plain-English template, not reviewed).
- [ ] Refund policy, privacy policy (the plugin collects nothing and makes no network calls today), VAT/sales tax via a merchant of record (Paddle, FastSpring, Gumroad, Plugin Boutique) is the easy way to avoid doing it yourself.
- [ ] Licence keys / activation: optional. Decide before beta so testers see the real flow.
