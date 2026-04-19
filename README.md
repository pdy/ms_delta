# ms_delta
Calculates and applies delta patches using windows's [MSDelta API](https://learn.microsoft.com/en-us/windows/win32/devnotes/msdelta).

# Usage
```
Creates delta diffs between name matching files in two folders.
General usage:
        ms_delta <command> -s <source> -t <target> -p <patches>

        <command> - create/apply
        -s,--source    - source catalog path REQUIRED
        -t,--target    - target catalog path REQUIRED
        -p,--patches   - patches catalog path REQUIRED
        -e,--extension - patches file extension, ".patch" by default OPTIONAL
Example create:
        ms_delta create -s "C:\source_data" -t "C:\target_data" -p "C:\patches"

Example apply:
        ms_delta apply -s "C:\source_data" -t "C:\applied_data_patches" -p "C:\patches"
```
