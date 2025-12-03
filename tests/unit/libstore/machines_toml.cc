#include "lix/libutil/file-system.hh"
#include "lix/libstore/machines.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/strings.hh"

#include <gmock/gmock-matchers.h>

using testing::Contains;
using testing::ElementsAre;
using testing::EndsWith;
using testing::Eq;
using testing::Field;
using testing::HasSubstr;
using testing::SizeIs;

using nix::absPath;
using nix::FormatError;
using nix::getMachines;
using nix::Machine;
using nix::Machines;
using nix::pathExists;
using nix::settings;
using nix::UsageError;

TEST(machines, getMachinesTOMLWithEmptyBuilders)
{
    settings.builders.override("");
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(0));
}

TEST(machines, getMachinesTOMLUriOnly)
{
    settings.builders.override(
        "[machines.scratchy]\n"
        "uri = \"ssh://nix@scratchy.labs.cs.uu.nl\""
    );
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, Eq("ssh://nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("TEST_ARCH-TEST_OS")));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::sshPublicHostKey, SizeIs(0)));
}

TEST(machines, getMachinesTOMLMultipleMachines)
{
    settings.builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "[machines.itchy]\n"
        "uri = \"nix@itchy.labs.cs.uu.nl\"\n"
    );
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(2));
    EXPECT_THAT(
        actual, Contains(Field(&Machine::storeUri, EndsWith("nix@scratchy.labs.cs.uu.nl")))
    );
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, EndsWith("nix@itchy.labs.cs.uu.nl"))));
}

TEST(machines, getMachinesTOMLWithCorrectCompleteSingleBuilder)
{
    settings.builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "system-types = [\"i686-linux\"]\n"
        "ssh-key = \"/home/nix/.ssh/id_scratchy_auto\"\n"
        "jobs = 8\n"
        "speed-factor = 3.0\n"
        "supported-features = [\"kvm\"]\n"
        "mandatory-features = [\"benchmark\"]\n"
        "ssh-public-host-key = \"ssh-ed25519 "
        "AAAAC3NzaC1lZDI1NTE5AAAAIJYfqESaiQlOrL3Wm1Q9s9q8b4mjj2nIuyqCZub5aGPi nix@scratchy\"\n"
    );
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, EndsWith("nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("i686-linux")));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, Eq("/home/nix/.ssh/id_scratchy_auto")));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(8)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(3)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, ElementsAre("kvm")));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, ElementsAre("benchmark")));
    EXPECT_THAT(
        actual[0],
        Field(
            &Machine::sshPublicHostKey,
            Eq("c3NoLWVkMjU1MTkgQUFBQUMzTnphQzFsWkRJMU5URTVBQUFBSUpZZnFFU2FpUWxPckwzV20xUTlzOXE4YjR"
               "tamoybkl1eXFDWnViNWFHUGkgbml4QHNjcmF0Y2h5")
        )
    );
}

TEST(machines, getMachinesTOMLBothFloatFormats)
{
    settings.builders.override(
        "[machines.andesite]\n"
        "uri = \"ssh://lix@andesite.lix.systems\"\n"
        "speed-factor = 3\n"
    );
    auto actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(3)));
    settings.builders.override(
        "[machines.diorite]\n"
        "uri = \"ssh://lix@diorite.lix.systems\"\n"
        "speed-factor = 3.1\n"
    );
    actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(3.1f)));
}

TEST(machines, getMachinesTOMLWithMultiOptions)
{
    settings.builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "system-types = [\"Arch1\", \"Arch2\"]\n"
        "supported-features = [\"SupportedFeature1\", \"SupportedFeature2\"]\n"
        "mandatory-features = [\"MandatoryFeature1\", \"MandatoryFeature2\"]\n"
    );
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, EndsWith("nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("Arch1", "Arch2")));
    EXPECT_THAT(
        actual[0],
        Field(&Machine::supportedFeatures, ElementsAre("SupportedFeature1", "SupportedFeature2"))
    );
    EXPECT_THAT(
        actual[0],
        Field(&Machine::mandatoryFeatures, ElementsAre("MandatoryFeature1", "MandatoryFeature2"))
    );
}

#define EXPECT_MESSAGE_THROW(EXPR, EXC, MSG)           \
    EXPECT_THROW(                                      \
        {                                              \
            try {                                      \
                EXPR;                                  \
            } catch (const EXC & e) {                  \
                EXPECT_THAT(e.what(), HasSubstr(MSG)); \
                throw;                                 \
            }                                          \
        },                                             \
        EXC                                            \
    );

TEST(machines, getMachinesTOMLExtraKeys)
{
    settings.builders.override(
        "[machines.andesite]\n"
        "uri = \"ssh://lix@andesite.lix.systems\"\n"
        "extra-key = 3\n"
    );
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "unexpected key `extra-key`");
    settings.builders.override(
        "[machines.andesite]\n"
        "uri = \"ssh://lix@andesite.lix.systems\"\n"
        "another-key = 3\n"
    );
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "unexpected key `another-key`");
}

/*
 This should throw a syntax error, but actually parses successfully and puts weird shit in the uri
field instead Other parsers (e.g. pythons tomllib) do successfully throw a syntax error here, but
toml11 doesn't AAAAAAAAAAAAAAAAAAAAAA An upstream issue was created for this on 2025-12-01
(https://github.com/ToruNiina/toml11/issues/303)

Note: this somehow worked once or twice, but now its broken again (and CI agrees that it's broken)
*/
TEST(machines, getMachinesTOMLNoQuotationOnUri)
{
    GTEST_SKIP() << "See upstream issue https://github.com/ToruNiina/toml11/issues/303";
    settings.builders.override(
        "[machines.invalid_syntax]\n"
        "uri = ssh://lix@andesite.lix.systems\n"
        "maxJobs = -3\n"
    );
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "bad format: unknown value appeared");
}

TEST(machines, getMachinesTOMLWithIncorrectTyping)
{
    settings.builders.override("[machines.a]");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "uri must be present");
    settings.builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "jobs = -3\n"
    );
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "jobs must be >= 0");
    settings.builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "jobs = \"three\"\n"
    );
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "bad_cast to integer");
    settings.builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "jobs = 8\n"
        "speed-factor = -3.0\n"
    );
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "speed factor must be >= 0");
    settings.builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "jobs = 8\n"
        "speed-factor = \"three\"\n"
    );
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "bad_cast to floating");

    settings.builders.override(
        "[[machines]]\n"
        "uri = \"lix@andesite.lix.systems\"\n"
        "[[machines]]\n"
        "uri = \"lix@diorite.lix.systems\"\n"
    );
    EXPECT_MESSAGE_THROW(
        getMachines(),
        UsageError,
        "Expected key `machines` to be a table of name -> machine configurations"
    );

    settings.builders.override("machines.a = \"lix@andesite.lix.sytems\"\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "Each machine must be a table");

    settings.builders.override(
        "version = \"1\"\n"
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
    );
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "bad_cast to integer");

    settings.builders.override(
        "version = 1\n"
        "[machines.legacy]\n"
        "uri = \"ssh://nix@nix-15-11.nixos.org\"\n"
        "enable = 0\n"
    );
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "bad_cast to boolean");
}

TEST(machines, getMachinesTOMLBadVersion)
{
    settings.builders.override(
        "version = \"hello\"\n"
        "machines = {}\n"
    );
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "bad_cast to integer");
}

TEST(machines, getMachinesTOMLTooHighVersion)
{
    settings.builders.override(
        "version = 42\n"
        "machines = {}\n"
    );
    EXPECT_MESSAGE_THROW(
        getMachines(),
        UsageError,
        "Unable to parse Machines of version 42, only versions between 1 and 1 are supported."
    );
}

TEST(machines, getMachinesTOMLTooLowVersion)
{
    settings.builders.override(
        "version = -1\n"
        "machines = {}\n"
    );
    EXPECT_MESSAGE_THROW(
        getMachines(),
        UsageError,
        "Unable to parse Machines of version -1, only versions between 1 and 1 are supported."
    );
}

TEST(machines, getMachinesTOMLInvalidSyntaxButClearlyTOML)
{
    settings.builders.override(
        "version = 1\n"
        "[machines]\n"
        "[machines.hello]\n"
        "uri = \"ssh://hello\"\n"
        " = 5\n"
    );
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "invalid Machines TOML syntax:");
}

TEST(machines, getMachinesTOMLOneDisabled)
{
    settings.builders.override(
        "version = 1\n"
        "[machines.a]\n"
        "uri = \"ssh://test\"\n"
        "enable = false\n"
        "\n"
        "[machines.b]\n"
        "uri = \"ssh://test2\"\n"
    );
    auto actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, EndsWith("test2")));
}

#undef EXPECT_MESSAGE_THROW
