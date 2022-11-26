#define WORKSPACE GetEnv('WORKSPACE')
#define GIX_REVISION GetEnv('GIX_REVISION')
#define VER_GIXSQLMAJ GetEnv('GIXSQLMAJ')
#define VER_GIXSQLMIN GetEnv('GIXSQLMIN')
#define VER_GIXSQLREL GetEnv('GIXSQLREL')

#define REDIST_DIR GetEnv('REDIST_DIR')

#define DIST_DIR GetEnv('DIST_DIR')

#define INCLUDE_VS GetEnv('INCLUDE_VS')

#define CONFIG "Release"
#define HOST_PLATFORM GetEnv('HOST_PLATFORM')
#define MSVC_RUNTIME_X86 GetEnv('MSVC_RUNTIME_X86')
#define MSVC_RUNTIME_X64 GetEnv('MSVC_RUNTIME_X64')

#define P7ZIP "https://www.7-zip.org/a/7zr.exe"

[Setup]
AppName=GixSQL
AppVersion={#VER_GIXSQLMAJ}.{#VER_GIXSQLMIN}.{#VER_GIXSQLREL}-{#GIX_REVISION}
AppCopyright=Marco Ridoni
DefaultDirName={pf}\GixSQL
OutputDir={#WORKSPACE}\deploy\installers\gixsql-{#HOST_PLATFORM}
OutputBaseFilename=gixsql-{#VER_GIXSQLMAJ}.{#VER_GIXSQLMIN}.{#VER_GIXSQLREL}-{#GIX_REVISION}-installer
ArchitecturesInstallIn64BitMode=x64
DefaultGroupName=GixSQL
LicenseFile={#WORKSPACE}\LICENSE
RestartIfNeededByRun=False
DisableWelcomePage=False

[Files]
; main binaries
Source: "{#DIST_DIR}\bin\gixpp.exe"; DestDir: "{app}\bin"; Flags: ignoreversion createallsubdirs recursesubdirs
Source: "{#DIST_DIR}\lib\*"; DestDir: "{app}\lib"; Flags: ignoreversion createallsubdirs recursesubdirs

; COPY files
Source: "{#WORKSPACE}\copy\SQLCA.cpy"; DestDir: "{app}\lib\copy"; Flags: ignoreversion createallsubdirs recursesubdirs

; examples and docs
Source: "{#WORKSPACE}\gixsql-tests-nunit\data\*.cbl"; DestDir: "{userdocs}\GixSQL\Examples"; Flags: ignoreversion createallsubdirs recursesubdirs onlyifdoesntexist
Source: "{#WORKSPACE}\gixsql-tests-nunit\data\*.cpy"; DestDir: "{userdocs}\GixSQL\Examples"; Flags: ignoreversion createallsubdirs recursesubdirs onlyifdoesntexist
Source: "{#WORKSPACE}\gixsql-tests-nunit\data\*.sql"; DestDir: "{userdocs}\GixSQL\Examples"; Flags: ignoreversion createallsubdirs recursesubdirs onlyifdoesntexist
Source: "{#WORKSPACE}\README"; DestDir: "{userdocs}\GixSQL\Documentation"; Flags: ignoreversion

; dependencies for DB runtime libraries
#if "x64" == HOST_PLATFORM
Source: "{#WORKSPACE}\gixsql\deploy\redist\pgsql\x64\msvc\*"; DestDir: "{app}\lib\x64\msvc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist
Source: "{#WORKSPACE}\gixsql\deploy\redist\pgsql\x64\gcc\*"; DestDir: "{app}\lib\x64\gcc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist

Source: "{#WORKSPACE}\gixsql\deploy\redist\mysql\x64\msvc\*"; DestDir: "{app}\lib\x64\msvc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist
Source: "{#WORKSPACE}\gixsql\deploy\redist\mysql\x64\gcc\*"; DestDir: "{app}\lib\x64\gcc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist
#endif

Source: "{#WORKSPACE}\gixsql\deploy\redist\pgsql\x86\msvc\*"; DestDir: "{app}\lib\x86\msvc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist
Source: "{#WORKSPACE}\gixsql\deploy\redist\pgsql\x86\gcc\*"; DestDir: "{app}\lib\x86\gcc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist

Source: "{#WORKSPACE}\gixsql\deploy\redist\mysql\x86\msvc\*"; DestDir: "{app}\lib\x86\msvc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist
Source: "{#WORKSPACE}\gixsql\deploy\redist\mysql\x86\gcc\*"; DestDir: "{app}\lib\x86\gcc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist

[Run]
#if "x64" == HOST_PLATFORM
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /passive /norestart"; WorkingDir: "{tmp}"; Flags: waituntilterminated skipifdoesntexist; Description: "Visual C++ 2022 redistributable package (x64)"
#endif
Filename: "{tmp}\vc_redist.x86.exe"; Parameters: "/install /passive /norestart"; WorkingDir: "{tmp}"; Flags: waituntilterminated skipifdoesntexist; Description: "Visual C++ 2022 redistributable package (x86)"

[Registry]
Root: "HKLM"; Subkey: "Software\MediumGray\gixsql"; ValueType: string; ValueName: "version"; ValueData: "{#VER_GIXSQLMAJ}.{#VER_GIXSQLMIN}.{#VER_GIXSQLREL}-{#GIX_REVISION}"; Flags: createvalueifdoesntexist deletevalue uninsdeletekey
Root: "HKLM"; Subkey: "Software\MediumGray\gixsql"; ValueType: string; ValueName: "HomeDir"; ValueData: "{app}"; Flags: createvalueifdoesntexist deletevalue uninsdeletekey
Root: "HKA"; Subkey: "Software\MediumGray\gixsql"; ValueType: string; ValueName: "DataDir"; ValueData: "{localappdata}\Gix"; Flags: createvalueifdoesntexist deletevalue uninsdeletekey
Root: "HKLM"; Subkey: "Software\MediumGray\gixsql"; Flags: uninsdeletekey
Root: "HKA"; Subkey: "Software\MediumGray\gixsql"; Flags: uninsdeletekey

[Dirs]
Name: "{app}\bin"
Name: "{app}\lib\copy"
Name: "{app}\lib\{#HOST_PLATFORM}\msvc"
Name: "{app}\lib\{#HOST_PLATFORM}\gcc"
Name: "{userdocs}\GixSQL"
Name: "{userdocs}\GixSQL\Documentation"
Name: "{userdocs}\GixSQL\Examples"

[Code]

#include "7zip.iss.inc"

var
  DownloadPage: TDownloadWizardPage;
  AvailableCompilers: TArrayOfString;
  SelectedCompilers: TArrayOfString;
  
  CompilerListInitialized : Boolean;
  ChooseCompilersPage: TWizardPage;
  DefaultCompilerPage: TInputOptionWizardPage;
  DefaultCompilerId: String;
  CheckListBox: TNewCheckListBox;
  cbGCC, cbMSVC : Integer;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  if Progress = ProgressMax then
    Log(Format('Successfully downloaded file to {tmp}: %s', [FileName]));
  Result := True;
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), @OnDownloadProgress);
  
end;

procedure CurPageChanged(CurPageID: Integer);
var
  i: Integer;
	cbtmp : Integer;
	crow : String;
  is_checked : Boolean;
	release_tag, id, version, host, target, linker, description : String;
begin
//  if (CurPageID = wpFinished) and (IsMSVCCompilerSelected) and (WizardForm.RunList.Items.Count > 0) then
//  begin
//    WizardForm.RunList.ItemEnabled[0] := False;
//  end;
//
//  if CurPageID = DefaultCompilerPage.ID then
//  begin
//    DefaultCompilerPage.CheckListBox.Items.Clear;
//    for i := 0 to GetArrayLength(SelectedCompilers) -1 do
//    begin
//      if not ParseCompilerEntry(SelectedCompilers[i], release_tag, id, version, host, target, linker, description) then continue;
//
//      DefaultCompilerPage.Add(description);
//    end;
//  end;
//  
//  if CurPageID = ChooseCompilersPage.ID then
//    begin
//    
//    CheckListBox.Items.Clear;
//    cbMSVC := CheckListBox.AddCheckBox('GnuCOBOL - Linker type: MSVC', '', 0, False, True, False, True, nil);
//    for i := 0 to GetArrayLength(AvailableCompilers) - 1 do 
//    begin
//      crow := AvailableCompilers[i];
//      if not ParseCompilerEntry(crow, release_tag, id, version, host, target, linker, description) then continue;
//      if linker <> 'msvc' then continue;
//      
//      is_checked := IsCompilerSelected(crow);
//      cbtmp := CheckListBox.AddCheckBox(description, '', 1, is_checked, True, False, True, TObject(i));
//      Log('Adding compiler: ' + description);
//    end;  
//        
//    cbGCC := CheckListBox.AddCheckBox('GnuCOBOL - Linker type: GCC/MinGW', '', 0, False, True, False, True, nil);    
//    for i := 0 to GetArrayLength(AvailableCompilers) - 1 do 
//    begin
//      crow := AvailableCompilers[i];
//      if not ParseCompilerEntry(crow, release_tag, id, version, host, target, linker, description) then continue;
//      if linker <> 'gcc' then continue;
//
//      is_checked := IsCompilerSelected(crow);      
//      cbtmp := CheckListBox.AddCheckBox(description, '', 1, is_checked, True, False, True, TObject(i));
//      Log('Adding compiler: ' + description);
//    end;  
//  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  i, chk_count, cidx : Integer;
  crow, curl: String;
  release_tag, id, version, host, target, linker, description : String;
begin
   
  if CurPageID = wpReady then begin
    DownloadPage.Clear;
    
    DownloadPage.Add('{#P7ZIP}', '7zr.exe', '');
    DownloadPage.Add('{#MSVC_RUNTIME_X86}', 'vc_redist.x86.exe', '');
    if '{#HOST_PLATFORM}' = 'x64' then
    begin
      DownloadPage.Add('{#MSVC_RUNTIME_X64}', 'vc_redist.x64.exe', '');
    end;
    
    DownloadPage.Show;
    try
      try
        DownloadPage.Download; 
        // TODO: check signatures        
        
        Result := True;
      except
        if DownloadPage.AbortedByUser then
          Log('Aborted by user.')
        else
          SuppressibleMsgBox(AddPeriod(GetExceptionMessage), mbCriticalError, MB_OK, IDOK);
        Result := False;
      end;
    finally
      DownloadPage.Hide;
    end;
  end else
    Result := True;
	
end;
