$ErrorActionPreference = 'stop'

Function Test-Command {
    Param ($Name)
    Try {
        if (Get-Command $Name) {
            return $True
        }
    }
    Catch {
    }
    return $False
}

Function Test-Admin {
    $currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

Write-Host "Hello, Developer! Let's see what's going on..."

while ($True) {
    $chocoNeeded = $False
    $canGenProject = $True

    if (!(Test-Command wsl)) {
        Write-Host "WSL not found"
        Write-Host "Follow this link to get inforamtion about how to install WSL2"
        Write-Host "https://docs.microsoft.com/en-us/windows/wsl/install-win10"
        $canGenProject = $False
    }

    # Todo: check WSL vetsion
    # Todo: check WSL active distro

    $msbuild = ./BuildTools/get-msbuild.ps1
    if (!$msbuild) {
        Write-Host "MSBuild not found"
        Write-Host "If you planning development in Visual Studio 2019 then install it"
        Write-Host "But if you don't need whole IDE then you can install just Build Tools for Visual Studio 2019"
        Write-Host "All this stuff you can get here: https://visualstudio.microsoft.com/downloads"
        Write-Host "(chocovs) Or install Visual Studio 2019 Community Edition automatically within Chocolatey"
        Write-Host "(chocobt) Or install Visual Studio Build Tools 2019 automatically within Chocolatey"
        $chocoNeeded = $True
        $canGenProject = $False
    }

    if (!(Test-Command cmake)) {
        Write-Host "CMake not found"
        Write-Host "You can get it here: https://cmake.org"
        Write-Host "(chococmake) Or install CMake automatically within Chocolatey"
        $chocoNeeded = $True
        $canGenProject = $False
    }

    # Todo: check VSCode
    # Todo: check WiX Toolset

    if ($chocoNeeded -And !(Test-Command choco)) {
        Write-Host "Chocolatey can install all necessary stuff automatically"
        Write-Host "Additional information you can find here: https://chocolatey.org"
        Write-Host "How to install Chocolatey you can find here: https://chocolatey.org/install"
        Write-Host "(choco) Or you can install Chocolatey automatically here and now"
    }

    if ($canGenProject) {
        Write-Host "(gen) Seems to all is fine, we can generate new project now"
    }

    while ($True) {
        $answer = Read-Host -Prompt "Type command or leave empty to exit setup"
        if ($answer -Eq "choco") {
            if (!Test-Admin) {
                Write-Host "To install Chocolatey here you must run this setup under administrative privileges"
            } else {
                if ((Get-ExecutionPolicy) -Eq "Restricted") {
                    Set-ExecutionPolicy AllSigned
                }
                Set-ExecutionPolicy Bypass -Scope Process -Force
                [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
                iex ((New-Object System.Net.WebClient).DownloadString("https://chocolatey.org/install.ps1"))
                break
            }
        } elseif (($answer.Length -Gt 5) -And ($answer.Substring(0, 5) -Eq "choco")) {
            if (!Test-Admin) {
                Write-Host "To install Chocolatey packages you must run this setup under administrative privileges"
            } elseif (Test-Command choco) {
                if ($answer -Eq "chocovs") {
                    choco install -y visualstudio2019community
                } elseif ($answer -Eq "chocobt") {
                    choco install -y visualstudio2019buildtools
                } elseif ($answer -Eq "chococmake") {
                    choco install -y cmake
                }
                break
            } else {
                Write-Host "(choco) Chocolatey not found, please install it first"
            }
        } elseif ($answer -Eq "gen") {
            Write-Host "...Generate new project..."
            # Todo:
        } elseif ($answer -Eq "") {
            Write-Host "Good luck!"
            return
        } else {
            Write-Host "Invalid command"
        }
    }
}