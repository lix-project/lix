#pragma once
/// @file

namespace nix {

void registerNixDaemon();
void registerLegacyNixDaemon();

class Config;

extern Config & daemonAuthorizationSettings;
}
