param([Parameter(Mandatory = $true)][string]$Directory)
$ErrorActionPreference = 'Stop'
[Console]::Error.WriteLine("TLS fixture: PowerShell $($PSVersionTable.PSVersion), compiling HTTPS server.")
Add-Type -Path (Join-Path $PSScriptRoot 'https_server.cs')
[Console]::Error.WriteLine('TLS fixture: generating certificates.')
[McrHttpsFixture]::Run($Directory).GetAwaiter().GetResult()
