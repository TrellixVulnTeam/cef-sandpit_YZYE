// Copyright (c) 2012 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/cefclient/renderer/client_renderer.h"

#include <stdio.h>
#include <algorithm>

#include <sstream>
#include <string>

#include "include/cef_crash_util.h"
#include "include/cef_dom.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_message_router.h"

namespace client {
namespace renderer {

namespace {

class ChromeV8Accessor : public CefV8Accessor {
 public:
  ChromeV8Accessor() {}

  virtual bool Get(const CefString& name,
                   const CefRefPtr<CefV8Value> object,
                   CefRefPtr<CefV8Value>& retval,
                   CefString& exception) override {
    FILE* file = fopen("sandpit.log", "a");
    if (file) {
      std::stringstream ss;
      ss << "access to field: " << name.ToString() << '\n';
      fputs(ss.str().c_str(), file);
      fclose(file);
    }

    // Value does not exist.
    return false;
  }

  virtual bool Set(const CefString& name,
                   const CefRefPtr<CefV8Value> object,
                   const CefRefPtr<CefV8Value> value,
                   CefString& exception) override {
    // Value does not exist.
    return false;
  }

  // Variable used for storing the value.
  CefString myval_;

  // Provide the reference counting implementation for this class.
  IMPLEMENT_REFCOUNTING(ChromeV8Accessor);
};

class ChromeV8Handler : public CefV8Handler {
 public:
  ChromeV8Handler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
    FILE* file = fopen("/tmp/sandpit.log", "a");
    if (file) {
      std::stringstream ss;
      ss << "chrome access: "
         << "[" << name.ToString() << "] ";
      std::for_each(arguments.begin(), arguments.end(),
                    [&ss](const CefRefPtr<CefV8Value>& arg) {
                      ss << '|' << arg->GetStringValue().ToString() << "|, ";
                    });
      ss << "\n";
      fputs(ss.str().c_str(), file);
      fclose(file);
    }
    return true;
  }

  // Provide the reference counting implementation for this class.
  IMPLEMENT_REFCOUNTING(ChromeV8Handler);
};

// Must match the value in client_handler.cc.
const char kFocusedNodeChangedMessage[] = "ClientRenderer.FocusedNodeChanged";

class ClientRenderDelegate : public ClientAppRenderer::Delegate {
 public:
  ClientRenderDelegate() : last_node_is_editable_(false) {}

  void OnWebKitInitialized(CefRefPtr<ClientAppRenderer> app) override {
    if (CefCrashReportingEnabled()) {
      // Set some crash keys for testing purposes. Keys must be defined in the
      // "crash_reporter.cfg" file. See cef_crash_util.h for details.
      CefSetCrashKeyValue("testkey_small1", "value1_small_renderer");
      CefSetCrashKeyValue("testkey_small2", "value2_small_renderer");
      CefSetCrashKeyValue("testkey_medium1", "value1_medium_renderer");
      CefSetCrashKeyValue("testkey_medium2", "value2_medium_renderer");
      CefSetCrashKeyValue("testkey_large1", "value1_large_renderer");
      CefSetCrashKeyValue("testkey_large2", "value2_large_renderer");
    }

    // Create the renderer-side router for query handling.
    CefMessageRouterConfig config;
    message_router_ = CefMessageRouterRendererSide::Create(config);

    // Define the extension contents.
    std::string extensionCode =
        "(function() {"
        "})();";

    // Register the extension.
    CefRegisterExtension("v8/test", extensionCode, nullptr);
  }

  void OnContextCreated(CefRefPtr<ClientAppRenderer> app,
                        CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override {
    CefRefPtr<CefV8Value> object = context->GetGlobal();

    // CefRefPtr<CefV8Accessor> accessor = new ChromeV8Accessor;
    // CefRefPtr<CefV8Value> chrome = CefV8Value::CreateObject(accessor,
    // nullptr);

    CefRefPtr<CefV8Handler> handler = new ChromeV8Handler();
    CefRefPtr<CefV8Value> chrome_intercept =
        CefV8Value::CreateFunction("chromeIntercept", handler);

    object->SetValue("chromeIntercept", chrome_intercept,
                     V8_PROPERTY_ATTRIBUTE_NONE);

    // CefRefPtr<CefV8Value> str = CefV8Value::CreateString("My Value!");
    // object->SetValue("myvalue", str, V8_PROPERTY_ATTRIBUTE_NONE);

    frame->ExecuteJavaScript(
        "(function() {"
        "window.chromeIntercept('targetKeys', ...Object.keys(window.chrome));"
        "window.chrome = new Proxy(window.chrome, {"
        "get: function (target, propKey, receiver) {"
        "window.chromeIntercept('get', propKey);"
        "return target[propKey];"
        "},"
        "ownKeys: function(target) {"
        "window.chromeIntercept('ownKeys', ...Object.keys(target));"
        "return [...Object.keys(target)];"
        // , 'setSyncEncryptionKeys', "
        // "'addTrustedSyncEncryptionRecoveryMethod', 'runtime', 'app'];"
        "},"
        "apply: function(target, thisArg, args) {"
        "window.chromeIntercept('apply', args);"
        "return target.apply(thisArg, args);"
        "},"
        "has: function(target, key) {"
        "window.chromeIntercept('has', key);"
        "return key in target;"
        "}"
        "});"
        "})();",
        frame->GetURL(), 0);

    message_router_->OnContextCreated(browser, frame, context);
  }

  void OnContextReleased(CefRefPtr<ClientAppRenderer> app,
                         CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override {
    message_router_->OnContextReleased(browser, frame, context);
  }

  void OnFocusedNodeChanged(CefRefPtr<ClientAppRenderer> app,
                            CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefDOMNode> node) override {
    bool is_editable = (node.get() && node->IsEditable());
    if (is_editable != last_node_is_editable_) {
      // Notify the browser of the change in focused element type.
      last_node_is_editable_ = is_editable;
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kFocusedNodeChangedMessage);
      message->GetArgumentList()->SetBool(0, is_editable);
      frame->SendProcessMessage(PID_BROWSER, message);
    }
  }

  bool OnProcessMessageReceived(CefRefPtr<ClientAppRenderer> app,
                                CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override {
    return message_router_->OnProcessMessageReceived(browser, frame,
                                                     source_process, message);
  }

 private:
  bool last_node_is_editable_;

  // Handles the renderer side of query routing.
  CefRefPtr<CefMessageRouterRendererSide> message_router_;

  DISALLOW_COPY_AND_ASSIGN(ClientRenderDelegate);
  IMPLEMENT_REFCOUNTING(ClientRenderDelegate);
};

}  // namespace

void CreateDelegates(ClientAppRenderer::DelegateSet& delegates) {
  delegates.insert(new ClientRenderDelegate);
}

}  // namespace renderer
}  // namespace client
