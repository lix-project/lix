#pragma once
///@file

#if __linux__

#include "lix/libutil/json.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/logging.hh"

namespace nix {

namespace systemd {

// Machine readable information returned by the hostname component from systemd (hostnamectl).
struct HostInformation
{
    std::string hostname;
    std::optional<std::string> chassis;
    std::string kernel_release;
    std::string kernel_version;
    std::string os_pretty_name;
    std::optional<std::string> hardware_vendor;
    std::optional<std::string> hardware_model;
    std::optional<std::string> hardware_version;
    std::optional<std::string> firmware_vendor;
    std::optional<std::string> firmware_version;
    std::optional<std::string> firmware_date;
    std::optional<std::string> build_id;
};

static void setIfNonNull(const JSON & j, std::optional<std::string> & target, const std::string & key)
{
    auto val = valueAt(j, key);
    if (!val.is_null()) {
        target = ensureType(val, nlohmann::detail::value_t::string);
    }
}

// Internal data structure that systemd returns for hostnamectl.
// This needs to be kept update with the range of supported versions of systemd for Lix.
struct SystemdHostname
{
    std::string Hostname;
    std::optional<std::string> Chassis;
    std::string KernelRelease;
    std::string KernelVersion;
    std::string OperatingSystemPrettyName;
    std::vector<std::string> OperatingSystemReleaseData;
    std::optional<std::string> HardwareVendor;
    std::optional<std::string> HardwareModel;
    std::optional<std::string> HardwareVersion;
    std::optional<std::string> FirmwareVersion;
    std::optional<std::string> FirmwareVendor;
    std::optional<std::string> FirmwareDate;

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
        setIfNonNull(j, raw.HardwareVendor, "HardwareVendor");
        setIfNonNull(j, raw.HardwareModel, "HardwareModel");
        setIfNonNull(j, raw.HardwareVersion, "HardwareVersion");
        setIfNonNull(j, raw.FirmwareVersion, "FirmwareVersion");
        setIfNonNull(j, raw.FirmwareVendor, "FirmwareVendor");

        auto version = valueAt(j, "FirmwareDate");
        if (!version.is_null()) {
            raw.FirmwareDate = std::to_string((uint64_t) ensureType(version, value_t::number_unsigned));
        }
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
