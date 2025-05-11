#include "lix/libexpr/value/context.hh"

namespace nix {

NixStringContextElem NixStringContextElem::parse(std::string_view s0)
{
    std::string_view s = s0;

    if (s.size() == 0) {
        throw BadNixStringContextElem(s0,
            "String context element should never be an empty string");
    }

    switch (s.at(0)) {
    case '!': {
        // Advance string to parse after the '!'
        s = s.substr(1);

        // Find *second* '!'
        size_t index = s.find("!");
        if (index == std::string_view::npos) {
            throw BadNixStringContextElem(s0,
                "String content element beginning with '!' should have a second '!'");
        }

        std::string output { s.substr(0, index) };
        // Advance string to parse after the '!'
        s = s.substr(index + 1);
        auto drv = SingleDerivedPath::Opaque{StorePath{s}};
        return SingleDerivedPath::Built{
            .drvPath = std::move(drv),
            .output = std::move(output),
        };
    }
    case '=': {
        return NixStringContextElem::DrvDeep {
            .drvPath = StorePath { s.substr(1) },
        };
    }
    default: {
        // Ensure no '!'
        if (s.find("!") != std::string_view::npos) {
            throw BadNixStringContextElem(s0,
                "String content element not beginning with '!' should not have a second '!'");
        }
        return SingleDerivedPath::Opaque{
            .path = StorePath{s},
        };
    }
    }
}

std::string NixStringContextElem::to_string() const
{
    std::string res;

    std::visit(overloaded {
        [&](const NixStringContextElem::Built & b) {
            res += '!';
            res += b.output;
            res += '!';
            res += b.drvPath.path.to_string();
        },
        [&](const NixStringContextElem::Opaque & o) {
            res += o.path.to_string();
        },
        [&](const NixStringContextElem::DrvDeep & d) {
            res += '=';
            res += d.drvPath.to_string();
        },
    }, raw);

    return res;
}

}
