#if __linux__

#include "libutil/systemd.hh"
#include "libutil/json-fwd.hh"
namespace nix {

namespace systemd {

void from_json(const JSON & j, SystemdHostname & h)
{
    h = SystemdHostname::parse(j);
}

kj::Promise<Result<std::optional<HostInformation>>> get_host_information()
try {
    auto output = TRY_AWAIT(runProgram("hostnamectl", true, {"--json=short"}));
    SystemdHostname raw = json::parse(output);
    std::optional<std::string> buildId;

    for (const auto & line : raw.OperatingSystemReleaseData) {
        if (line.starts_with("BUILD_ID=")) {
            buildId = line.substr(9);

            // Clean up quotes if they are present.
            if (buildId->size() >= 2 && buildId->front() == '"' && buildId->back() == '"') {
                buildId = buildId->substr(1, buildId->length() - 2);
            }
            break;
        }
    }

    HostInformation host_info = {
        .hostname = raw.Hostname,
        .chassis = raw.Chassis,
        .kernel_release = raw.KernelRelease,
        .kernel_version = raw.KernelVersion,
        .os_pretty_name = raw.OperatingSystemPrettyName,
        .hardware_vendor = raw.HardwareVendor,
        .hardware_model = raw.HardwareModel,
        .hardware_version = raw.HardwareVersion,
        .firmware_vendor = raw.FirmwareVendor,
        .firmware_version = raw.FirmwareVersion,
        .firmware_date = raw.FirmwareDate,
        .build_id = buildId,
    };

    co_return {host_info};
} catch (Error & e) {
    printTaggedWarning("could not get host information via hostnamectl: %s", e.msg());
    co_return {{}};
} catch (...) {
    co_return result::current_exception();
}
};

};

#endif
