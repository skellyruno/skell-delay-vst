; Inno Setup script. Build with:  iscc /DAppVersion=1.0.0 /DBuildDir=..\..\build SkellDelay.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build"
#endif
[Setup]
AppName=Skell Delay
AppVersion={#AppVersion}
AppPublisher=MyAudioDev
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
OutputDir=..\..\dist
OutputBaseFilename=SkellDelay-{#AppVersion}-Windows
Compression=lzma2
SolidCompression=yes
LicenseFile=..\..\legal\EULA.txt
[Files]
; the .vst3 is a folder bundle
Source: "{#BuildDir}\DelayPlugin_artefacts\Release\VST3\Skell Delay.vst3\*"; DestDir: "{commoncf64}\VST3\Skell Delay.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\docs\MANUAL.md"; DestDir: "{commonappdata}\Skell-Delay"; Flags: ignoreversion
