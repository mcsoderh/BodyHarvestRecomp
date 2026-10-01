Before building the Flatpak, place `bh.us.z64` in the root directory and build the patches there first. **The LLVM Extension for freedesktop does not include the MIPS compiler and will fail to build the patches inside the flatpak**.
```sh
make -C patches CC=clang LD=ld.lld
```

Build
```sh
flatpak-builder --force-clean --user --install-deps-from=flathub --repo=repo --install builddir io.github.bodyharvestrecomp.bodyharvestrecomp.json
```

Bundle
```sh
flatpak build-bundle repo io.github.bodyharvestrecomp.bodyharvestrecomp.flatpak io.github.bodyharvestrecomp.bodyharvestrecomp --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo
```

