#pragma once

#include <memory>

class IBase : public std::enable_shared_from_this<IBase>
{
public:
    virtual ~IBase() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;

    template <typename T>
    std::shared_ptr<T> GetShared()
    {
        return std::static_pointer_cast<T>(shared_from_this());
    }

    template <typename T>
    std::weak_ptr<T> GetWeak()
    {
        return std::shared_ptr<T>(GetShared<T>());
    }
};