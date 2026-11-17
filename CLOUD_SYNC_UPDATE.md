# B-machine update

Pull feature/random-turbo and rebuild the Windows application before use. The checked-in dist executable predates these source fixes; do not deploy it without rebuilding.

Preserve local user configurations. Inspect the existing CMakeCache.txt to locate the compiler and CMake/Ninja toolchain. Build the existing build directory, run applicable offline regressions, and deploy the resulting antimicrox.exe and required runtime files into the installation actually used on B. Exit the old process and restart the deployed executable.

Changes: Save and open iCloud persists settings before opening the cloud dialog; the selected scheme name supplies the snapshot name; snapshot filenames preserve readable names and add timestamps; Open sync folder opens the chosen directory. A device type mismatch restores into a matching offline workspace without requiring a physical controller. Close the cloud dialog to use the workspace.

Validation: the corresponding Windows changes compiled successfully on A and the deployed executable hash matched the build output. Offline regression execution on A was blocked by Windows elevation requirements. Cross-machine restoration and the reconstructed source in this commit still require validation on B.
