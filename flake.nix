{
  description = "vfs-verifier - Acorn VFS (Domesday) image verifier (Nix flake)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        packageVersion = "1.0.0";

        vfs-verifier = pkgs.stdenv.mkDerivation {
          pname = "vfs-verifier";
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
            runHook postInstallCheck
          '';

          meta = with pkgs.lib; {
            description = "Virtual File System data verifier for Domesday LaserDisc images";
            homepage = "https://github.com/domesday86/vfs-verifier";
            license = licenses.gpl3Plus;
            mainProgram = "vfs-verifier";
            platforms = platforms.unix;
          };
        };
      in
      {
        packages.default = vfs-verifier;
        packages.vfs-verifier = vfs-verifier;

        apps.default = {
          type = "app";
          program = "${vfs-verifier}/bin/vfs-verifier";
          meta = vfs-verifier.meta;
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ vfs-verifier ];
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
