[Setup]
; AppId 保持稳定：产品改名不应让旧版本变成"另一个应用"。
AppId=ClassTime.Desktop
AppName=“上课”时间到！
AppVersion={#ClassTimeVersion}
AppVerName=“上课”时间到！ {#ClassTimeVersion}
AppPublisher=It's Class Time!
AppComments=桌面作息时间组件：分节进度、课表编辑与系统提醒
DefaultDirName={localappdata}\Programs\ClassTime
; The desktop-shortcut option lives on this page, so keep it visible on upgrades too.
DisableDirPage=no
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableWelcomePage=no
PrivilegesRequired=lowest
; The setup mitigation is inherited by the runtime installer and by ClassTime
; when launched from Finish. It can block App Runtime package paths (0x800701C0).
RedirectionGuard=no
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.17763
AppMutex=ClassTime.Desktop.Running
SetupMutex=ClassTime.Desktop.Setup
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={app}\ClassTimeApp.exe
SetupIconFile=..\Assets\ClassTime.ico
WizardStyle=modern dynamic polar hidebevels
WizardSizePercent=110,107
WizardImageFile=ProgressPoster.png
WizardImageFileDynamicDark=ProgressPosterDark.png
WizardSmallImageFile=..\Assets\ClassTimeIcon.png
WizardSmallImageFileDynamicDark=..\Assets\ClassTimeIcon.png
UsePreviousAppDir=yes
ShowLanguageDialog=no
Compression=lzma2
SolidCompression=yes
OutputDir={#ReleaseDir}
OutputBaseFilename=ItsClassTime-{#ClassTimeVersion}-Windows-x64-Setup
VersionInfoCompany=It's Class Time!
VersionInfoDescription=“上课”时间到！ 安装程序
VersionInfoProductName=It's Class Time!
VersionInfoVersion={#ClassTimeVersion}
SetupLogging=yes

[Languages]
Name: "zhcn"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Messages]
SetupWindowTitle=“上课”时间到！ {#ClassTimeVersion} 安装程序

; 欢迎页
WelcomeLabel1=安装“上课”时间到！ {#ClassTimeVersion}
WelcomeLabel2=把课程表放到桌面边缘：随时知道现在第几节、这一节还剩多久，上下课与休息时收到系统通知。%n%n不需要账号，也不会联网。安装只针对当前 Windows 用户，不需要管理员权限；运行环境已随安装包内置。

; 安装位置（同时承载桌面快捷方式选项）
WizardSelectDir=安装位置
SelectDirDesc=选择“上课”时间到！的程序文件存放位置。
SelectDirLabel3=程序会安装到这里；课表与设置单独保存，升级或卸载都不会删除。
SelectDirBrowseLabel=如需更换位置，请点击“浏览”。确认后点击“安装”开始。

; 完成页
FinishedHeadingLabel=安装完成
FinishedLabel=“上课”时间到！会在屏幕边缘显示当前进度，并常驻系统托盘；关闭设置窗口不会退出。%n%n右键托盘图标，可以打开设置、切换显示样式、暂停提醒或退出。第一次使用建议先选好高中或大学模板，再按自己的课表调整。

; 应用正在运行
SetupAppRunningError=“上课”时间到！正在运行，安装程序需要先把它关掉。%n%n请右键系统托盘中的图标，选择“退出”，然后点击“确定”继续。
UninstallAppRunningError=“上课”时间到！正在运行，卸载程序需要先把它关掉。%n%n请右键系统托盘中的图标，选择“退出”，然后点击“确定”继续。

; 卸载
ConfirmUninstall=确认卸载“上课”时间到！%n%n程序文件会被删除；你的课表和设置保存在本地应用数据目录，不会被删除，重新安装后可以继续使用。
UninstalledAll=“上课”时间到！已卸载。%n%n课表与设置仍然保留，重新安装后可以直接继续使用。

[Files]
; Extract this separately so runtime failures stop Setup before app files are written.
Source: "{#RuntimeInstaller}"; DestName: "WindowsAppRuntimeInstall-x64.exe"; Flags: dontcopy noencryption
Source: "{#AppBuildDir}\ClassTimeApp.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BootstrapDll}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\shared\presets\high-school.json"; DestDir: "{app}\presets"; Flags: ignoreversion
Source: "..\..\shared\presets\university.json"; DestDir: "{app}\presets"; Flags: ignoreversion
Source: "{#RuntimeLicense}"; DestDir: "{app}"; DestName: "Windows-App-SDK-LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\“上课”时间到！"; Filename: "{app}\ClassTimeApp.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\“上课”时间到！"; Filename: "{app}\ClassTimeApp.exe"; WorkingDir: "{app}"; Check: CreateDesktopShortcut

[Run]
Filename: "{app}\ClassTimeApp.exe"; Description: "立即启动“上课”时间到！"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
var
  RuntimePage: TOutputMarqueeProgressWizardPage;
  DesktopShortcut: TNewCheckBox;

procedure InitializeWizard;
begin
  DesktopShortcut := TNewCheckBox.Create(WizardForm);
  DesktopShortcut.Parent := WizardForm.SelectDirPage;
  DesktopShortcut.Caption := '同时创建桌面快捷方式';
  DesktopShortcut.Left := WizardForm.DirEdit.Left;
  DesktopShortcut.Top := WizardForm.DirEdit.Top + WizardForm.DirEdit.Height + ScaleY(18);
  DesktopShortcut.Width := WizardForm.DirEdit.Width;
  DesktopShortcut.Height := ScaleY(24);
  DesktopShortcut.TabOrder := WizardForm.DirBrowseButton.TabOrder + 1;
  DesktopShortcut.Checked := GetPreviousData('DesktopShortcut', '0') = '1';

  RuntimePage := CreateOutputMarqueeProgressPage(
    '正在准备运行环境', '安装包已内置 Windows App Runtime，无需联网下载。');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  // 就绪页已关闭，安装位置页的“下一步”直接开始安装。
  if CurPageID = wpSelectDir then
    WizardForm.NextButton.Caption := SetupMessage(msgButtonInstall);
end;

function CreateDesktopShortcut: Boolean;
begin
  Result := DesktopShortcut.Checked;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  if DesktopShortcut.Checked then
    SetPreviousData(PreviousDataKey, 'DesktopShortcut', '1')
  else
    SetPreviousData(PreviousDataKey, 'DesktopShortcut', '0');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
begin
  Result := '';
  RuntimePage.SetText('正在配置 Windows App Runtime 1.6…',
    '通常需要一两分钟，请稍候。');
  RuntimePage.Show;
  try
    RuntimePage.Animate;
    ExtractTemporaryFile('WindowsAppRuntimeInstall-x64.exe');
    if not Exec(ExpandConstant('{tmp}\WindowsAppRuntimeInstall-x64.exe'), '--quiet', '',
      SW_HIDE, ewWaitUntilTerminated, ExitCode) then
      Result := '无法启动 Windows App Runtime 安装程序。' + #13#10 +
        '请确认安全软件没有拦截安装包，然后重新运行安装程序。'
    else if ExitCode <> 0 then
      Result := Format('Windows App Runtime 安装未完成（错误码 0x%.8x）。' + #13#10 +
        '请重启电脑后重试；若仍然失败，请重新下载安装包。', [ExitCode]);
  finally
    RuntimePage.Hide;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command: String;
  RunKey: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    RunKey := 'Software\Microsoft\Windows\CurrentVersion\Run';
    if RegQueryStringValue(HKCU, RunKey, 'ClassTime.Desktop', Command) and
       (CompareText(Command, '"' + ExpandConstant('{app}\ClassTimeApp.exe') + '"') = 0) then
      RegDeleteValue(HKCU, RunKey, 'ClassTime.Desktop');
  end;
end;
