#pragma once

#include "AppConfig.h"

#include <memory>

class Renderer {
public:
    explicit Renderer(const RunOptions& options);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    int Run();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
