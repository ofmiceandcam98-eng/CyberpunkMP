#pragma once

#include "Core/Foundation/Application.hpp"
#include <RED4ext/Api/v1/Sdk.hpp>


namespace App
{
struct Application : Core::Application
{
    explicit Application(RED4ext::v1::PluginHandle aPlugin, const RED4ext::v1::Sdk* aSdk = nullptr);

protected:

    void OnStarted() override;
    void OnStopped() override;
};

extern UniquePtr<Application> GApplication;
}


