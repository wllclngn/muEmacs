# μEmacs Windows 11 PowerShell Profile Integration
# Place this in: %USERPROFILE%\Documents\PowerShell\Microsoft.PowerShell_profile.ps1

# Windows 11 Fluent Design PowerShell styling
$PSStyle.FileInfo.Directory = "$($PSStyle.Bold)$($PSStyle.Foreground.Blue)"
$PSStyle.Progress.Style = "$($PSStyle.Background.Blue)$($PSStyle.Foreground.White)"

# μEmacs Environment Setup
$env:UEMACS_THEME = "windows11"
$env:EDITOR = "uemacs"

# VSCode Terminal Detection
if ($env:TERM_PROGRAM -eq "vscode") {
    $env:UEMACS_VSCODE = "1"
    Write-Host "● μEmacs integrated with VSCode" -ForegroundColor Blue
}

# Berkeley Mono Font Optimization
$env:UEMACS_FONT = "Berkeley Mono"

# Quick μEmacs aliases (matching user's pattern)
function ue { uemacs $args }
function uemacs-session { uemacs --load-session }

# Windows 11 native file associations
if (Get-Command uemacs -ErrorAction SilentlyContinue) {
    # Associate common text files with μEmacs
    $textExtensions = @('.txt', '.md', '.c', '.h', '.py', '.js', '.go', '.rs')
    
    foreach ($ext in $textExtensions) {
        if (-not (Get-ItemProperty -Path "HKCU:\SOFTWARE\Classes\$ext" -ErrorAction SilentlyContinue)) {
            # Only set if not already associated
            New-Item -Path "HKCU:\SOFTWARE\Classes\$ext" -Force | Out-Null
            Set-ItemProperty -Path "HKCU:\SOFTWARE\Classes\$ext" -Name "(default)" -Value "μEmacs.Document"
        }
    }
    
    # Create μEmacs document class
    $uemacsClass = "HKCU:\SOFTWARE\Classes\μEmacs.Document"
    if (-not (Test-Path $uemacsClass)) {
        New-Item -Path $uemacsClass -Force | Out-Null
        Set-ItemProperty -Path $uemacsClass -Name "(default)" -Value "μEmacs Document"
        
        New-Item -Path "$uemacsClass\shell\open\command" -Force | Out-Null
        $uemacsPath = (Get-Command uemacs).Source
        Set-ItemProperty -Path "$uemacsClass\shell\open\command" -Name "(default)" -Value "`"$uemacsPath`" `"%1`""
    }
}

# Windows 11 enhanced prompt (subtle integration)
function prompt {
    $currentPath = $PWD.Path.Replace($env:USERPROFILE, "~")
    $gitBranch = ""
    
    # Git integration (if in git repo)
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $gitStatus = git branch --show-current 2>$null
        if ($gitStatus) {
            $gitBranch = " ($gitStatus)"
        }
    }
    
    # Windows 11 Fluent Design inspired prompt
    Write-Host "● " -NoNewline -ForegroundColor Blue
    Write-Host "$currentPath" -NoNewline -ForegroundColor White
    Write-Host "$gitBranch" -NoNewline -ForegroundColor Green
    Write-Host " " -NoNewline
    
    return "> "
}

# μEmacs session restoration on startup
if ($env:UEMACS_AUTO_SESSION -eq "1") {
    Write-Host "● μEmacs session ready" -ForegroundColor Green
}

# Windows 11 clipboard integration
function Copy-ToClipboard {
    param([string]$Text)
    Set-Clipboard -Value $Text
    Write-Host "● Copied to Windows clipboard" -ForegroundColor Green
}

function Get-FromClipboard {
    return Get-Clipboard
}

# Export functions for μEmacs integration
Export-ModuleMember -Function Copy-ToClipboard, Get-FromClipboard

Write-Host "● μEmacs Windows 11 PowerShell profile loaded" -ForegroundColor Blue