# Helper script to push to GitHub bypassing Intel proxy
# Usage: .\push.ps1

$env:NO_PROXY = "github.com"
git push origin main
Remove-Item Env:\NO_PROXY
