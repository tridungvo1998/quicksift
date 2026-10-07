Get-ChildItem -Recurse -Include *.cpp, *.h, *.hpp | ForEach-Object { 
    "--- START FILE: " + $_.FullName.Replace((Get-Location).Path + "\", "") + " ---`n" + 
    (Get-Content $_ -Raw) + "`n" + 
    "--- END FILE ---`n`n" 
} | Out-File -FilePath codebase_bundle.txt -Encoding utf8