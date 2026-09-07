{
  description = "vfs-tools - Acorn VFS (Domesday) image verifier and stacker (Nix flake)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        packageVersion = "1.0.0";

        vfs-tools = pkgs.stdenv.mkDerivation {
          pname = "vfs-tools";
          version = packageVersion;

          src = pkgs.lib.cleanSourceWith {
            src = ./.;
            filter = path: type:
              let
                base = pkgs.lib.baseNameOf path;
              in
                !(base == ".git" || base == "build" || base == "result");
          };

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
          ];

          buildInputs = with pkgs; [
            spdlog
            fmt
          ];

          cmakeBuildType = "Release";
          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];

          doInstallCheck = true;
          installCheckPhase = ''
            runHook preInstallCheck
            $out/bin/vfs-verifier --help > /dev/null
            $out/bin/vfs-stacker --help > /dev/null
            runHook postInstallCheck
          '';

          meta = with pkgs.lib; {
            description = "Virtual File System data verifier and stacker for Domesday LaserDisc images";
            homepage = "https://github.com/domesday86/vfs-verifier";
            license = licenses.gpl3Plus;
            mainProgram = "vfs-verifier";
            platforms = platforms.unix;
          };
        };
      in
      {
        packages.default = vfs-tools;
        packages.vfs-tools = vfs-tools;

        # Both tools ship in the one derivation; these names are kept so that
        # `nix build .#vfs-verifier` and `nix build .#vfs-stacker` still work
        packages.vfs-verifier = vfs-tools;
        packages.vfs-stacker = vfs-tools;

        apps.default = {
          type = "app";
          program = "${vfs-tools}/bin/vfs-verifier";
          meta = vfs-tools.meta;
        };

        apps.vfs-verifier = {
          type = "app";
          program = "${vfs-tools}/bin/vfs-verifier";
          meta = vfs-tools.meta // { mainProgram = "vfs-verifier"; };
        };

        apps.vfs-stacker = {
          type = "app";
          program = "${vfs-tools}/bin/vfs-stacker";
          meta = vfs-tools.meta // { mainProgram = "vfs-stacker"; };
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ vfs-tools ];
          packages = with pkgs; [
            cmake
            ninja
            pkg-config
            gdb
            clang-tools
          ];
        };

        formatter = pkgs.nixpkgs-fmt;
      }
    );
}
