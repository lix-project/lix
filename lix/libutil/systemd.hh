#pragma once
///@file

#if __linux__

#include "lix/libutil/types.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/logging.hh"

namespace nix {

namespace systemd {

// Machine readable information returned by the hostname component from systemd (hostnamectl).
struct HostInformation
{
    std::string hostname;
    std::string chassis;
    std::string kernel_release;
    std::string kernel_version;
    std::string os_pretty_name;
    std::string hardware_vendor;
    std::string hardware_model;
    std::string hardware_version;
    std::string firmware_vendor;
    std::string firmware_version;
    std::string firmware_date;
    std::optional<std::string> build_id;
};

// Internal data structure that systemd returns for hostnamectl.
// This needs to be kept update with the range of supported versions of systemd for Lix.
struct SystemdHostname
{
    std::string Hostname;
    std::string Chassis;
    std::string KernelRelease;
    std::string KernelVersion;
    std::string OperatingSystemPrettyName;
    std::vector<std::string> OperatingSystemReleaseData;
    std::string HardwareVendor;
    std::string HardwareModel;
    std::string HardwareVersion;
    std::string FirmwareVersion;
    std::string FirmwareVendor;
    std::string FirmwareDate;

    static SystemdHostname parse(const JSON & j)
    {
        using nlohmann::detail::value_t;

        SystemdHostname raw;

        ensureType(j, value_t::object);
        raw.Hostname = ensureType(valueAt(j, "Hostname"), value_t::string);
        raw.Chassis = ensureType(valueAt(j, "Chassis"), value_t::string);
        raw.KernelRelease = ensureType(valueAt(j, "KernelRelease"), value_t::string);
        raw.KernelVersion = ensureType(valueAt(j, "KernelVersion"), value_t::string);
        raw.OperatingSystemPrettyName = ensureType(valueAt(j, "OperatingSystemPrettyName"), value_t::string);
        raw.HardwareVendor = ensureType(valueAt(j, "HardwareVendor"), value_t::string);
        raw.HardwareModel = ensureType(valueAt(j, "HardwareModel"), value_t::string);
        raw.HardwareVersion = ensureType(valueAt(j, "HardwareVersion"), value_t::string);
        raw.FirmwareVersion = ensureType(valueAt(j, "FirmwareVersion"), value_t::string);
        raw.FirmwareVendor = ensureType(valueAt(j, "FirmwareVendor"), value_t::string);
        raw.FirmwareDate =
            std::to_string((uint64_t) ensureType(valueAt(j, "FirmwareDate"), value_t::number_unsigned));
        const JSON & osReleaseRaw = valueAt(j, "OperatingSystemReleaseData");
        ensureType(osReleaseRaw, value_t::array);
        std::vector<JSON> osRelease = (std::vector<JSON>) osReleaseRaw;
        for (const auto & item : osRelease) {
            raw.OperatingSystemReleaseData.push_back(ensureType(item, value_t::string));
        }

        return raw;
    }
};

void from_json(const JSON & j, SystemdHostname & h);

kj::Promise<Result<std::optional<HostInformation>>> get_host_information();
};
};

#endif
