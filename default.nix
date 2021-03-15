{ pkgs ? import <nixpkgs> {} }:
pkgs.callPackage ./hydra.nix {}
