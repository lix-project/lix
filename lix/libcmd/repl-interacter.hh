#pragma once
/// @file

#include "lix/libutil/types.hh"
#include <memory>
#include <string>

namespace rust::lix::repl {
struct Rustyline;
}

namespace nix {

namespace detail {
/** Provides the completion hooks for the repl, without exposing its complete
 * internals. */
struct ReplCompleterMixin {
    virtual StringSet completePrefix(const std::string & prefix) = 0;
};
};

enum class ReplPromptType {
    ReplPrompt,
    ContinuationPrompt,
};

class ReplInteracter
{
public:
    virtual void init(detail::ReplCompleterMixin * repl) {}
    /** Returns a boolean of whether the interacter got EOF */
    virtual bool getLine(std::string & input, ReplPromptType promptType) = 0;
    virtual ~ReplInteracter(){};
};

class ReadlineLikeInteracter final : public ReplInteracter
{
    std::string historyFile;
    std::unique_ptr<rust::lix::repl::Rustyline> rl;

public:
    ReadlineLikeInteracter(std::string historyFile);

    virtual void init(detail::ReplCompleterMixin * repl) override;
    virtual bool getLine(std::string & input, ReplPromptType promptType) override;
    /** Writes the current history to the history file.
     *
     * This function logs but ignores errors from readline's write_history().
     */
    void writeHistory();
    virtual ~ReadlineLikeInteracter() override;
};

class AutomationInteracter final : public ReplInteracter
{
public:
    AutomationInteracter() = default;
    virtual bool getLine(std::string & input, ReplPromptType promptType) override;
    virtual ~AutomationInteracter() override = default;
};

};
