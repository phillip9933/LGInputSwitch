#pragma once
#include <string>
namespace lg
{
// History describes accepted commands in this session, never monitor readback.
class InputHistory
{
    std::string current_, previous_;

  public:
    void sent(const std::string &input)
    {
        if (input != current_)
        {
            previous_ = current_;
            current_ = input;
        }
    }
    const std::string &previous() const
    {
        return previous_;
    }
    void reset()
    {
        current_.clear();
        previous_.clear();
    }
};
} // namespace lg
