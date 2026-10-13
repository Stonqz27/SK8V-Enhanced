# Native monochrome Enhanced setup. Windows PowerShell 5.1 / WinForms.
# -SelfTest uses a fake worker; -RenderDirectory exports actual UI states.
# Neither option prepares assets or writes to a GTA installation.
param([string]$RenderDirectory = '', [switch]$SelfTest)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -ReferencedAssemblies System.Windows.Forms, System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
namespace EnhancedSetup {
    public class Shell : Form {
        public Shell() { DoubleBuffered = true; }
    }
    public static class Native {
        [DllImport("gdi32.dll", CharSet=CharSet.Unicode)]
        public static extern int AddFontResourceEx(string path, uint flags, IntPtr reserved);
        [DllImport("gdi32.dll", CharSet=CharSet.Unicode)]
        public static extern bool RemoveFontResourceEx(string path, uint flags, IntPtr reserved);
        [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
        [DllImport("dwmapi.dll")] public static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
    }
    public class BoardArt : Control {
        public BoardArt() { DoubleBuffered = true; }
        protected override void OnPaint(PaintEventArgs e) {
            base.OnPaint(e);
            Graphics g = e.Graphics; g.SmoothingMode = SmoothingMode.AntiAlias;
            g.ScaleTransform(Width / 240f, Height / 140f);
            g.TranslateTransform(120, 68); g.RotateTransform(-17); g.TranslateTransform(-120, -68);
            using (Pen dim = new Pen(Color.FromArgb(65, 65, 65), 1))
            using (Pen white = new Pen(Color.FromArgb(240, 240, 240), 2))
            using (GraphicsPath deck = new GraphicsPath()) {
                for (int i=0; i<6; i++) g.DrawLine(dim, 35, 106+i*4, 205, 106+i*4);
                deck.AddBezier(22, 58, 2, 29, 50, 35, 72, 43);
                deck.AddBezier(72, 43, 110, 54, 150, 53, 174, 44);
                deck.AddBezier(174, 44, 236, 16, 240, 60, 206, 70);
                deck.AddBezier(206, 70, 151, 90, 65, 92, 22, 58);
                deck.CloseFigure(); g.DrawPath(white, deck);
                g.DrawBezier(dim, 24, 65, 70, 102, 172, 92, 210, 77);
                g.DrawLine(white, 70, 86, 70, 97); g.DrawLine(white, 183, 78, 183, 88);
                g.DrawEllipse(white, 58, 94, 22, 22); g.DrawEllipse(white, 173, 87, 22, 22);
                g.DrawEllipse(dim, 65, 101, 8, 8); g.DrawEllipse(dim, 180, 94, 8, 8);
                g.DrawLine(dim, 68, 61, 82, 63); g.DrawLine(dim, 170, 57, 185, 53);
            }
        }
    }
    // C# callbacks run outside the PowerShell UI runspace. Queue output for its timer.
    public sealed class Job : IDisposable {
        readonly object gate = new object();
        readonly Queue<string> lines = new Queue<string>();
        readonly StreamWriter log;
        public readonly Process Process;
        public static string Quote(string value) {
            StringBuilder b = new StringBuilder("\""); int slashes = 0;
            foreach (char c in value) {
                if (c == '\\') { slashes++; continue; }
                if (c == '"') b.Append('\\', slashes * 2 + 1);
                else b.Append('\\', slashes);
                b.Append(c); slashes = 0;
            }
            b.Append('\\', slashes * 2); b.Append('"'); return b.ToString();
        }
        void Receive(object sender, DataReceivedEventArgs e) {
            if (e.Data == null) return;
            lock (gate) { log.WriteLine(e.Data); log.Flush(); lines.Enqueue(e.Data); }
        }
        public Job(string program, string[] args, string directory, string logPath) {
            log = new StreamWriter(new FileStream(logPath, FileMode.CreateNew, FileAccess.Write, FileShare.Read), new UTF8Encoding(false));
            Process = new Process();
            Process.StartInfo = new ProcessStartInfo(program) {
                WorkingDirectory = directory, UseShellExecute = false, CreateNoWindow = true,
                RedirectStandardOutput = true, RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8
            };
            string[] quoted = Array.ConvertAll(args, Quote);
            Process.StartInfo.Arguments = String.Join(" ", quoted);
            Process.OutputDataReceived += Receive; Process.ErrorDataReceived += Receive;
            try { Process.Start(); Process.BeginOutputReadLine(); Process.BeginErrorReadLine(); }
            catch { log.Dispose(); Process.Dispose(); throw; }
        }
        public string[] Drain() {
            lock (gate) { string[] result = lines.ToArray(); lines.Clear(); return result; }
        }
        public void FinishReading() { Process.WaitForExit(); }
        public void Dispose() { lock (gate) { log.Dispose(); } Process.Dispose(); }
    }
}
'@
if (-not $RenderDirectory -and -not $SelfTest) { [void][EnhancedSetup.Native]::SetProcessDPIAware() }
[Windows.Forms.Application]::EnableVisualStyles()
$Root = Split-Path -Parent $PSScriptRoot
$privateFonts = New-Object Drawing.Text.PrivateFontCollection
$fontPaths = @('SpaceGrotesk-Regular.ttf', 'SpaceGrotesk-Bold.ttf') | ForEach-Object { Join-Path $PSScriptRoot "setup-ui\$_" }
foreach ($path in $fontPaths) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing setup font: $path. Extract the whole release ZIP." }
    $privateFonts.AddFontFile($path)
    [void][EnhancedSetup.Native]::AddFontResourceEx($path, 16, [IntPtr]::Zero)
}
$family = $privateFonts.Families | Where-Object Name -eq 'Space Grotesk' | Select-Object -First 1
if (-not $family) { throw 'The bundled Space Grotesk font could not be loaded.' }
$fonts = New-Object 'System.Collections.Generic.List[System.Drawing.Font]'
function Font([float]$size, [string]$style = 'Regular') {
    $value = New-Object Drawing.Font($family, $size, [Drawing.FontStyle]$style)
    $fonts.Add($value); return $value
}
function Grey([int]$value) { [Drawing.Color]::FromArgb($value, $value, $value) }
$white = Grey 245; $muted = Grey 156; $dim = Grey 90; $paper = Grey 11; $surface = Grey 22
$form = New-Object EnhancedSetup.Shell -Property @{
    Text = 'SK8V Enhanced / Setup'; ClientSize = '960,704'; StartPosition = 'CenterScreen'
    FormBorderStyle = 'FixedSingle'; MaximizeBox = $false; BackColor = $paper
    ForeColor = $white; Font = (Font 10); AutoScaleMode = 'Font'
}
function Add([string]$type, [hashtable]$props, $parent = $form) {
    $control = New-Object "Windows.Forms.$type" -Property $props
    $parent.Controls.Add($control); return $control
}
function Label($parent, [string]$text, [int]$x, [int]$y, [int]$w, [int]$h, [float]$size = 10, [string]$style = 'Regular', $color = $white) {
    Add Label @{Text=$text; Location="$x,$y"; Size="$w,$h"; Font=(Font $size $style); ForeColor=$color; UseCompatibleTextRendering=$true} $parent
}
function Button($parent, [string]$text, [int]$x, [int]$y, [int]$w, [int]$h, [bool]$primary = $false) {
    $button = Add Button @{Text=$text; Location="$x,$y"; Size="$w,$h"; FlatStyle='Flat'; Font=(Font 10 'Bold'); Cursor=[Windows.Forms.Cursors]::Hand; UseCompatibleTextRendering=$true} $parent
    $button.BackColor = $(if ($primary) {$white} else {$surface})
    $button.ForeColor = $(if ($primary) {$paper} else {$white})
    $button.FlatAppearance.BorderSize = 1; $button.FlatAppearance.BorderColor = Grey 54
    $button.FlatAppearance.MouseOverBackColor = $(if ($primary) {Grey 216} else {Grey 38})
    return $button
}
function Link($parent, [string]$text, [int]$x, [int]$y, [int]$w) {
    Add LinkLabel @{Text=$text; Location="$x,$y"; Size="$w,25"; Font=(Font 8.8); LinkColor=$muted; ActiveLinkColor=$white; VisitedLinkColor=$muted; DisabledLinkColor=$dim; UseCompatibleTextRendering=$true} $parent
}
$rail = Add Panel @{Location='0,0'; Size='296,704'; BackColor=(Grey 16)}
[void](Add Panel @{Location='295,0'; Size='1,704'; BackColor=(Grey 40)} $rail)
[void](Label $rail 'SK8V' 25 25 248 85 54 'Bold')
[void](Label $rail 'E N H A N C E D' 31 116 238 22 9 'Bold' $muted)
$art = New-Object EnhancedSetup.BoardArt -Property @{Location='29,156'; Size='239,140'; BackColor=$rail.BackColor}
$rail.Controls.Add($art)
[void](Label $rail "OWN`nTHE CITY." 29 311 242 105 29 'Bold')
[void](Label $rail "Skate 3 mechanics.`nGTA V's open world." 32 421 236 49 10 'Regular' $muted)
$stepLabels = @()
foreach ($step in 0..2) {
    $stepLabels += Label $rail (@('01   YOUR GAMES','02   SET UP','03   GO SKATE')[$step]) 32 (500+$step*37) 230 26 9 'Bold' $dim
}
[void](Label $rail "COMMUNITY PORT / STONQZ27`nBased on SK8V by Hoppi / becaring" 32 641 238 42 8 'Regular' $dim)
$content = Add Panel @{Location='330,34'; Size='596,574'; BackColor=$paper}
$source = Add Panel @{Dock='Fill'; BackColor=$paper} $content
$work = Add Panel @{Dock='Fill'; BackColor=$paper; Visible=$false} $content
[void](Label $source 'SETUP / 01' 0 0 540 25 9 'Bold' $muted)
[void](Label $source "YOUR GAMES.`nONE PLAYGROUND." 0 28 590 76 25 'Bold')
[void](Label $source 'Choose your files. Setup prepares the skating system locally.' 0 110 592 28 10 'Regular' $muted)
function PathRow([string]$title, [int]$y, [bool]$iso = $false) {
    [void](Label $source $title 0 $y 580 25 9 'Bold')
    $box = Add Panel @{Location="0,$($y+30)"; Size='596,44'; BackColor=$surface} $source
    $text = Add TextBox @{Location='12,11'; Size='460,25'; BorderStyle='None'; BackColor=$surface; ForeColor=$white; Font=(Font 10); AccessibleName=$title} $box
    $browse = Button $box 'Browse' 482 5 108 34
    $browse.Add_Click({
        if ($iso) { $dialog = New-Object Windows.Forms.OpenFileDialog -Property @{Filter='Skate 3 Xbox 360 ISO (*.iso)|*.iso'; Title='Select your own Skate 3 disc image'} }
        else { $dialog = New-Object Windows.Forms.FolderBrowserDialog -Property @{Description=$title; ShowNewFolderButton=$false} }
        try {
            if ($dialog.ShowDialog($form) -eq 'OK') { $text.Text = $(if ($iso) {$dialog.FileName} else {$dialog.SelectedPath}) }
        } finally { $dialog.Dispose() }
    }.GetNewClosure())
    return $text
}
$game = PathRow 'GTA V ENHANCED FOLDER' 152
$disc = PathRow 'SKATE 3 / XBOX 360 DISC' 237 $true
$extracted = Link $source 'Use an extracted disc folder instead' 1 314 330
$extracted.Add_LinkClicked({
    $dialog = New-Object Windows.Forms.FolderBrowserDialog -Property @{Description='Skate 3 disc folder with default.xex and data'; ShowNewFolderButton=$false}
    try { if ($dialog.ShowDialog($form) -eq 'OK') { $disc.Text = $dialog.SelectedPath } } finally { $dialog.Dispose() }
})
$walker = PathRow 'GEN9 CODEWALKER FOLDER' 350
[void](Label $source 'YOUR STANCE' 0 439 220 25 9 'Bold')
$regular = Button $source 'Regular' 0 467 132 38 $true
$goofy = Button $source 'Goofy' 141 467 132 38
[void](Label $source 'Your lead foot. You can change this later.' 289 476 304 30 9 'Regular' $muted)
$script:stance = 'Regular'
function Stance([string]$value) {
    $script:stance = $value
    foreach ($button in @($regular, $goofy)) {
        $selected = $button.Text -eq $value
        $button.BackColor = $(if ($selected) {$white} else {$surface})
        $button.ForeColor = $(if ($selected) {$paper} else {$white})
    }
}
$regular.Add_Click({ Stance 'Regular' }); $goofy.Add_Click({ Stance 'Goofy' })
$sourceMessage = Label $source "Your own assets. Prepared on your PC.`nKeep the extracted setup folder after installation." 0 518 596 46 9 'Regular' $muted

$eyebrow = Label $work 'SETUP / 02' 0 0 540 25 9 'Bold' $muted
$headline = Label $work "READY WHEN`nYOU ARE." 0 28 590 76 25 'Bold'
$subtitle = Label $work 'Review your choices, then let setup do its thing.' 0 110 592 34 10 'Regular' $muted
$review = Add Panel @{Location='0,154'; Size='596,406'; BackColor=$paper} $work
$summaries = @()
foreach ($i in 0..2) {
    [void](Label $review (@('GTA V ENHANCED','SKATE 3 DISC','CODEWALKER')[$i]) 0 ($i*62) 590 20 8.5 'Bold' $dim)
    $summaries += Add Label @{Location="0,$($i*62+23)"; Size='590,30'; Font=(Font 10); ForeColor=$white; AutoEllipsis=$true; UseCompatibleTextRendering=$true} $review
}
$download = Add CheckBox @{Text='Download portable Python if needed (about 31 MB)'; Location='0,197'; Size='590,29'; Checked=$true; Font=(Font 9.5); ForeColor=$white; FlatStyle='Flat'; UseCompatibleTextRendering=$true} $review
$graphics = Add CheckBox @{Text='Back up and replace existing ReShade / HUD files'; Location='0,232'; Size='590,29'; Font=(Font 9.5); ForeColor=$white; FlatStyle='Flat'; UseCompatibleTextRendering=$true; Visible=$false} $review
[void](Label $review "Close GTA before installing. Needs ScriptHookV + ASI loader, OpenRPF, .NET 9 x64 and Enhanced-capable CodeWalker.`n`nSetup checks your files, prepares assets and saves backups. Existing DLC entries are preserved." 0 290 590 96 9.5 'Regular' $muted)
$progressView = Add Panel @{Location='0,155'; Size='596,407'; BackColor=$paper; Visible=$false} $work
$status = Label $progressView 'Starting setup...' 0 0 590 53 14 'Bold'
$track = Add Panel @{Location='0,69'; Size='596,6'; BackColor=(Grey 42)} $progressView
$fill = Add Panel @{Location='0,0'; Size='0,6'; BackColor=$white} $track
$meta = Label $progressView '0% stage progress / 0:00 elapsed' 0 89 590 24 9 'Regular' $muted
$stageLabels = @()
$stageNames = @('Skate disc + animation data', 'Audio', 'GTA characters', 'Enhanced skateboard', 'Board / weapon DLC', 'Backups + installation')
foreach ($i in 0..5) { $stageLabels += Label $progressView ('--   '+$stageNames[$i]) 0 (145+$i*32) 590 27 10 'Regular' $dim }
$completion = Label $progressView '' 0 144 590 219 11 'Regular' $muted
$completion.Visible = $false
$log = Add TextBox @{Location='0,317'; Size='596,249'; Multiline=$true; ReadOnly=$true; ScrollBars='Both'; WordWrap=$false; BackColor=$surface; ForeColor=$white; BorderStyle='FixedSingle'; Font=(New-Object Drawing.Font('Consolas', 9)); Visible=$false} $work
[void](Add Panel @{Location='330,625'; Size='596,1'; BackColor=(Grey 40)})
$details = Link $form 'Show details' 330 650 106
$details.Enabled = $false
$guide = Link $form 'Setup guide' 445 650 105
$openLog = Link $form 'Open log' 551 650 90
$openLog.Visible = $false
$back = Button $form 'Back' 673 642 104 42
$back.Visible = $false
$next = Button $form 'Continue  >' 788 642 138 42 $true
$form.AcceptButton = $next
$restoreLink = Link $source 'Restore a previous install' 0 552 270
$script:page = 'source'; $script:job = $null; $script:percent = 0; $script:lastLine = ''
$script:errorText = ''; $script:logPath = ''; $script:action = 'install'; $script:started = Get-Date
function Page([string]$value) {
    $script:page = $value; $source.Visible = $value -eq 'source'; $work.Visible = $value -ne 'source'
    for ($i=0; $i -lt 3; $i++) {
        $current = $(if ($value -eq 'source') {0} elseif ($value -eq 'complete') {2} else {1})
        $stepLabels[$i].ForeColor = $(if ($i -eq $current) {$white} else {$dim})
    }
    $back.Visible = $value -in @('review', 'error')
    $next.Enabled = $value -ne 'running'
    $details.Enabled = $value -in @('running', 'complete', 'error')
    $next.Text = switch ($value) {source {'Continue  >'} review {'Install'} running {'Working...'} complete {'Finish'} error {'Try again'}}
}
function Inputs {
    $values = @($game.Text, $disc.Text, $walker.Text) | ForEach-Object { $_.Trim().Trim('"') }
    if (-not $values[0] -or -not (Test-Path -LiteralPath (Join-Path $values[0] 'GTA5_Enhanced.exe'))) { throw 'Choose the GTA folder containing GTA5_Enhanced.exe.' }
    if (-not $values[1] -or -not (Test-Path -LiteralPath $values[1])) { throw 'Choose your own Skate 3 Xbox 360 ISO or extracted disc folder.' }
    if (-not $values[2]) { throw 'Choose your Gen9 CodeWalker folder.' }
    foreach ($name in @('CodeWalker.Core.dll', 'SharpDX.dll', 'SharpDX.Mathematics.dll', 'ShadersGen9Conversion.xml')) {
        if (-not (Test-Path -LiteralPath (Join-Path $values[2] $name))) { throw "The CodeWalker folder is missing $name. See the setup guide." }
    }
    return $values
}
function Review {
    try { $values = Inputs } catch { $sourceMessage.Text = $_.Exception.Message; return }
    for ($i=0; $i -lt 3; $i++) { $summaries[$i].Text = $values[$i] }
    $graphics.Visible = @('dxgi.dll','ReShade.ini','SK8VHudBridge.addon64' | Where-Object { Test-Path -LiteralPath (Join-Path $values[0] $_) }).Count -gt 0
    $review.Visible = $true; $progressView.Visible = $false; $log.Visible = $false
    $headline.Text = "READY WHEN`nYOU ARE."; $subtitle.Text = "Stance: $script:stance. Backups are made before installation."
    $eyebrow.Text = 'SETUP / 02'; Page 'review'
}
function Update-Progress([int]$value) {
    $script:percent = [Math]::Max(0, [Math]::Min(100, $value))
    $fill.Width = [int]($track.Width*$script:percent/100)
    $thresholds = @(40,60,72,82,90,100)
    foreach ($i in 0..5) {
        $done = $script:percent -ge $thresholds[$i]
        $stageLabels[$i].Text = $(if ($done) {'OK   '} else {'--   '}) + $stageNames[$i]
        $stageLabels[$i].ForeColor = $(if ($done) {$white} else {$dim})
    }
}
function Consume([string[]]$lines) {
    foreach ($line in $lines) {
        if ($line.StartsWith('@sk8v ')) {
            try {
                $record = $line.Substring(6) | ConvertFrom-Json
                if ($record.phase -eq 'error') { $script:errorText = [string]$record.title }
                else { $status.Text = [string]$record.title; Update-Progress ([int]$record.progress) }
            } catch { $log.AppendText($line+"`r`n") }
        } else {
            $log.AppendText($line+"`r`n")
            if ($line.Trim()) { $script:lastLine = $line.Trim() }
            if ($line.StartsWith('Setup stopped:')) { $script:errorText = $line.Substring(14).Trim() }
        }
    }
    # The file retains everything; keep the on-screen tail bounded on long conversions.
    if ($log.TextLength -gt 100000) { $log.Text = $log.Text.Substring($log.TextLength-70000); $log.SelectionStart = $log.TextLength }
}
function Result([int]$code) {
    $stageLabels | ForEach-Object { $_.Visible = $false }
    $completion.Visible = $true; $openLog.Visible = [bool]$script:logPath
    if ($code -eq 0) {
        Update-Progress 100; $eyebrow.Text = 'SETUP / COMPLETE'
        if ($script:action -eq 'restore') {
            $headline.Text = "BACK TO YOUR`nPREVIOUS SETUP."; $status.Text = 'Previous files restored.'
            $completion.Text = "Your previous mod files are restored.`n`nPrepared assets and backups are retained. See the full log for the restored files."
        } else {
            $headline.Text = "THE CITY`nIS YOURS."; $status.Text = 'SK8V Enhanced is installed.'
            $completion.Text = "01   Launch GTA Enhanced Story Mode with BattlEye off.`n`n02   Select Skateboard from the melee weapon wheel.`n`n03   Tap Y to mount / dismount. Hold Y to put it away.`n`nKeep this setup folder. Your prepared assets live here."
        }
        $subtitle.Text = 'All done. Your setup log is saved below.'; Page 'complete'
    } else {
        $eyebrow.Text = 'SETUP / NEEDS ATTENTION'; $headline.Text = "LET'S FIX`nTHIS STEP."
        $status.Text = 'Setup stopped.'; $subtitle.Text = 'The full error is saved. Completed preparation stages can be reused.'
        $message = $(if ($script:errorText) {$script:errorText} elseif ($script:lastLine) {$script:lastLine} else {"Worker exited with code $code. Open the setup log."})
        $completion.Text = $message + "`n`nFix the reported issue, then try again. Installation failures attempt to restore the previous files; check the log for the result."
        Page 'error'
    }
    $elapsed = (Get-Date)-$script:started
    $meta.Text = '{0}% stage progress / {1:0}:{2:00} elapsed' -f $script:percent, [Math]::Floor($elapsed.TotalMinutes), $elapsed.Seconds
}
function Start-Job([string]$action, [string[]]$arguments) {
    $logDirectory = Join-Path $Root 'local\setup-logs'
    [void][IO.Directory]::CreateDirectory($logDirectory)
    $script:logPath = Join-Path $logDirectory (('setup-{0}-{1}.log' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), [Guid]::NewGuid().ToString('N').Substring(0,8)))
    $words = @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'enhanced-setup.ps1'),'--wizard-worker','--events') + $arguments
    $script:job = New-Object EnhancedSetup.Job((Join-Path $PSHOME 'powershell.exe'), [string[]]$words, $Root, $script:logPath)
    $script:started = Get-Date; $script:action = $action; $script:lastLine = ''; $script:errorText = ''
    $log.Clear(); $review.Visible = $false; $progressView.Visible = $true; $completion.Visible = $false
    $stageLabels | ForEach-Object { $_.Visible = $true }
    $log.Visible = $false; $details.Text = 'Show details'; $openLog.Visible = $true
    $headline.Text = "MAKING IT`nHAPPEN."; $subtitle.Text = 'This can take a few minutes. Your completed stages are saved.'
    $eyebrow.Text = 'SETUP / IN PROGRESS'; $status.Text = 'Checking the setup runtime...'
    Update-Progress 0; Page 'running'
}
function Tick {
    if (-not $script:job) { return }
    Consume ($script:job.Drain())
    $elapsed = (Get-Date)-$script:started
    $meta.Text = '{0}% stage progress / {1:0}:{2:00} elapsed' -f $script:percent, [Math]::Floor($elapsed.TotalMinutes), $elapsed.Seconds
    if ($script:job.Process.HasExited) {
        $script:job.FinishReading(); Consume ($script:job.Drain())
        $code = $script:job.Process.ExitCode; $script:job.Dispose(); $script:job = $null
        Result $code
    }
}
$timer = New-Object Windows.Forms.Timer -Property @{Interval=150}
$timer.Add_Tick({ try { Tick } catch { $status.Text = 'Could not read setup output. Open the log for details.' } })
$next.Add_Click({
    try {
        switch ($script:page) {
            'source' { Review }
            'complete' { $form.Close() }
            'error' { if ($script:action -eq 'restore') { Start-Job 'restore' @('--gta',$game.Text.Trim().Trim('"'),'--restore') } else { Review } }
            'review' {
                $values = Inputs
                $words = @('--gta',$values[0],'--skate',$values[1],'--codewalker',$values[2],'--stance',$script:stance)
                if ($download.Checked) { $words += '--download-python' }
                if ($graphics.Checked) { $words += '--allow-graphics-replacement' }
                Start-Job 'install' $words
            }
        }
    } catch { [void][Windows.Forms.MessageBox]::Show($form, $_.Exception.Message, 'SK8V Enhanced / Setup', 'OK', 'Information') }
})
$back.Add_Click({ Page 'source'; $log.Visible = $false; $details.Text = 'Show details' })
$details.Add_LinkClicked({ $log.Visible = -not $log.Visible; if ($log.Visible) {$log.BringToFront()}; $details.Text = $(if ($log.Visible) {'Hide details'} else {'Show details'}) })
$guide.Add_LinkClicked({
    $path = Join-Path $Root 'START-HERE.md'
    if (-not (Test-Path -LiteralPath $path)) { $path = Join-Path $Root 'docs\PLAYER-SETUP.md' }
    Start-Process notepad.exe -ArgumentList ([EnhancedSetup.Job]::Quote($path))
})
$openLog.Add_LinkClicked({ if ($script:logPath) { Start-Process notepad.exe -ArgumentList ([EnhancedSetup.Job]::Quote($script:logPath)) } })
$restoreLink.Add_LinkClicked({
    $path = $game.Text.Trim().Trim('"')
    if (-not $path -or -not (Test-Path -LiteralPath (Join-Path $path 'SK8V-Enhanced.installed.json'))) {
        $sourceMessage.Text = 'Choose a GTA folder with an SK8V Enhanced installation to restore.'; return
    }
    if ([Windows.Forms.MessageBox]::Show($form, 'Restore the previous mod files? Prepared assets and backups are kept.', 'SK8V Enhanced / Restore', 'YesNo', 'Question') -eq 'Yes') {
        try { Start-Job 'restore' @('--gta',$path,'--restore') } catch { $sourceMessage.Text = $_.Exception.Message }
    }
})
$form.Add_FormClosing({
    if ($script:job) {
        $_.Cancel = $true
        [void][Windows.Forms.MessageBox]::Show($form, 'Setup is still running. Let it finish before closing so asset preparation and backups can complete.', 'SK8V Enhanced / Setup', 'OK', 'Information')
    }
})
$form.Add_Shown({ $value=1; [void][EnhancedSetup.Native]::DwmSetWindowAttribute($form.Handle,20,[ref]$value,4) })
Page 'source'

# Windows-native rendering and fake-worker proof. No installation code is invoked.
function Render([string]$name) {
    if (-not $RenderDirectory) { return }
    [void][IO.Directory]::CreateDirectory($RenderDirectory)
    $form.CreateControl(); $form.PerformLayout()
    $bitmap = New-Object Drawing.Bitmap($form.Width, $form.Height)
    try { $form.DrawToBitmap($bitmap, (New-Object Drawing.Rectangle(0,0,$bitmap.Width,$bitmap.Height))); $bitmap.Save((Join-Path $RenderDirectory ($name+'.png')), [Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
}
try {
    if ($SelfTest -or $RenderDirectory) {
        # DrawToBitmap only paints visible descendants. Show an invisible test
        # window to create native handles without flashing over the user's apps.
        $form.Opacity = 0; $form.ShowInTaskbar = $false
        $form.Show(); [Windows.Forms.Application]::DoEvents()
        if (-not $source.Visible -or $work.Visible) { throw 'Initial source page was not visible.' }
        $game.Text = 'D:\Games\Grand Theft Auto V Enhanced'
        $disc.Text = 'D:\Owned Games\Skate 3.iso'; $walker.Text = 'C:\Tools\CodeWalker'
        Render '01-sources'
        $source.Visible = $false; $work.Visible = $true
        $summaries[0].Text = $game.Text; $summaries[1].Text = $disc.Text; $summaries[2].Text = $walker.Text
        $graphics.Visible = $true; Page 'review'; Render '02-review'
        $review.Visible = $false; $progressView.Visible = $true
        $headline.Text = "MAKING IT`nHAPPEN."; $status.Text = 'Reading GTA characters'; Update-Progress 60
        $meta.Text = '60% stage progress / 3:24 elapsed'; Page 'running'; Render '03-progress'
        $script:action = 'install'; Result 0; Render '04-complete'
        Update-Progress 60; $script:errorText = 'CodeWalker.Core.dll is missing from the selected directory.'; Result 2; Render '05-error'
        if ($SelfTest) {
            $testDir = Join-Path $Root ('build\wizard-tests\'+[Guid]::NewGuid().ToString('N'))
            [void][IO.Directory]::CreateDirectory($testDir)
            $worker = Join-Path $testDir 'worker.ps1'
            $fixture = @'
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
$OutputEncoding = [Console]::OutputEncoding
$args | ConvertTo-Json -Compress | Write-Output
Write-Output '@sk8v {"title":"Reading GTA characters","progress":60,"phase":"install"}'
[Console]::Error.WriteLine('fixture stderr retained')
Write-Output 'Setup stopped: fixture dependency failure'
exit 2
'@
            [IO.File]::WriteAllText($worker, $fixture)
            $expected = @('D:\Owned Games\Skate 3.iso', 'C:\folder & literal $name\', 'custom "quoted" ped', 'C:\caf'+[char]0xe9+'\disc')
            $job = New-Object EnhancedSetup.Job((Join-Path $PSHOME 'powershell.exe'), [string[]](@('-NoProfile','-File',$worker)+$expected), $Root, (Join-Path $testDir 'worker.log'))
            $output = New-Object 'System.Collections.Generic.List[string]'
            $deadline = (Get-Date).AddSeconds(30)
            while (-not $job.Process.WaitForExit(100)) {
                if ((Get-Date) -gt $deadline) { $job.Process.Kill(); $job.Dispose(); throw 'Fake worker timed out.' }
                foreach ($line in $job.Drain()) {$output.Add($line)}
            }
            $job.FinishReading(); foreach ($line in $job.Drain()) {$output.Add($line)}
            $exitCode = $job.Process.ExitCode; $job.Dispose()
            $actual = $output | Where-Object { $_.StartsWith('[') } | Select-Object -First 1 | ConvertFrom-Json
            if ($actual.Count -ne $expected.Count) { throw 'Worker argument count changed.' }
            for ($i=0; $i -lt $expected.Count; $i++) { if ($actual[$i] -cne $expected[$i]) { throw "Worker argument $i changed: $($actual[$i])" } }
            if ($exitCode -ne 2 -or -not ($output -contains 'fixture stderr retained')) { throw 'Worker error output or exit code lost.' }
            $script:errorText = ''; Consume $output.ToArray()
            if ($script:percent -ne 60 -or $script:errorText -ne 'fixture dependency failure') { throw 'Progress/error events were not consumed.' }
            Result $exitCode
            if ($script:page -ne 'error' -or $next.Text -ne 'Try again') { throw 'Failure did not offer a retry.' }
            Result 0
            if ($script:page -ne 'complete' -or $next.Text -ne 'Finish' -or $script:percent -ne 100) { throw 'Success did not complete.' }
            if ($family.Name -ne 'Space Grotesk') { throw 'Custom font missing.' }
            if (Test-Path -LiteralPath (Join-Path $Root 'release.json')) {
                # Exercise the real launcher too, against an empty fake game
                # directory. It must reject preflight, never enter preparation.
                Start-Job 'install' @('--gta',$testDir,'--preflight')
                $deadline = (Get-Date).AddSeconds(30)
                while ($script:job) {
                    if ((Get-Date) -gt $deadline) { $script:job.Process.Kill(); $script:job.Dispose(); $script:job=$null; throw 'Real preflight worker timed out.' }
                    Tick; [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 50
                }
                $expectedError = $script:errorText -eq 'Select the folder containing GTA5_Enhanced.exe, not GTA Legacy.' -or
                    $script:errorText -eq 'Close GTA completely before setup or restore.'
                if ($script:page -ne 'error' -or -not $expectedError) { throw ('Unexpected preflight failure: '+$script:errorText) }
                if (Test-Path -LiteralPath (Join-Path $testDir 'SK8V-Enhanced.installed.json')) { throw 'Preflight wrote an installation manifest.' }
                Write-Output 'PASS: packaged launcher reports a real preflight rejection through the UI, records its full error, never installs.'
            }
            Write-Output 'PASS: native UI renders, font loads, quoted/Unicode paths survive, stderr/log and exit code retained, progress/error/success states work.'
        }
    } else { $timer.Start(); [void]$form.ShowDialog() }
} finally {
    $timer.Stop(); $timer.Dispose(); $form.Dispose()
    foreach ($font in $fonts) { $font.Dispose() }
    foreach ($path in $fontPaths) { [void][EnhancedSetup.Native]::RemoveFontResourceEx($path,16,[IntPtr]::Zero) }
    $privateFonts.Dispose()
}
