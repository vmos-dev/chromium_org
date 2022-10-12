// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_APPLICATION_APPLICATION_DELEGATE_H_
#define MOJO_PUBLIC_APPLICATION_APPLICATION_DELEGATE_H_

#include <string>

#include "mojo/public/cpp/system/macros.h"

namespace mojo {

class ApplicationConnection;
class ApplicationImpl;

class ApplicationDelegate {
 public:
  ApplicationDelegate();
  virtual ~ApplicationDelegate();

  // Implement this method to create the specific subclass of
  // ApplicationDelegate. Ownership is taken by the caller. It will be deleted.
  static ApplicationDelegate* Create();

  virtual void Initialize(ApplicationImpl* app);

  // Override this method to configure what services a connection supports when
  // being connected to from an app.
  // return false to reject the connection entirely.
  virtual bool ConfigureIncomingConnection(ApplicationConnection* connection);

  // Override this method to configure what services a connection supports when
  // connecting to another app.
  // return false to reject the connection entirely.
  virtual bool ConfigureOutgoingConnection(ApplicationConnection* connection);

 private:
  MOJO_DISALLOW_COPY_AND_ASSIGN(ApplicationDelegate);
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_APPLICATION_APPLICATION_DELEGATE_H_
