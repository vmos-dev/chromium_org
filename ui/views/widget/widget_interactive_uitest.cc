// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/base/ui_base_switches.h"
#include "ui/events/event_processor.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gl/gl_surface.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_test_api.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/focus_manager_test.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/touchui/touch_selection_controller_impl.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/wm/public/activation_client.h"

#if defined(OS_WIN)
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/win/hwnd_util.h"
#endif

namespace views {
namespace test {

namespace {

// A View that closes the Widget and exits the current message-loop when it
// receives a mouse-release event.
class ExitLoopOnRelease : public View {
 public:
  ExitLoopOnRelease() {}
  virtual ~ExitLoopOnRelease() {}

 private:
  // Overridden from View:
  virtual void OnMouseReleased(const ui::MouseEvent& event) OVERRIDE {
    GetWidget()->Close();
    base::MessageLoop::current()->QuitNow();
  }

  DISALLOW_COPY_AND_ASSIGN(ExitLoopOnRelease);
};

// A view that does a capture on ui::ET_GESTURE_TAP_DOWN events.
class GestureCaptureView : public View {
 public:
  GestureCaptureView() {}
  virtual ~GestureCaptureView() {}

 private:
  // Overridden from View:
  virtual void OnGestureEvent(ui::GestureEvent* event) OVERRIDE {
    if (event->type() == ui::ET_GESTURE_TAP_DOWN) {
      GetWidget()->SetCapture(this);
      event->StopPropagation();
    }
  }

  DISALLOW_COPY_AND_ASSIGN(GestureCaptureView);
};

// A view that always processes all mouse events.
class MouseView : public View {
 public:
  MouseView()
      : View(),
        entered_(0),
        exited_(0),
        pressed_(0) {
  }
  virtual ~MouseView() {}

  virtual bool OnMousePressed(const ui::MouseEvent& event) OVERRIDE {
    pressed_++;
    return true;
  }

  virtual void OnMouseEntered(const ui::MouseEvent& event) OVERRIDE {
    entered_++;
  }

  virtual void OnMouseExited(const ui::MouseEvent& event) OVERRIDE {
    exited_++;
  }

  // Return the number of OnMouseEntered calls and reset the counter.
  int EnteredCalls() {
    int i = entered_;
    entered_ = 0;
    return i;
  }

  // Return the number of OnMouseExited calls and reset the counter.
  int ExitedCalls() {
    int i = exited_;
    exited_ = 0;
    return i;
  }

  int pressed() const { return pressed_; }

 private:
  int entered_;
  int exited_;

  int pressed_;

  DISALLOW_COPY_AND_ASSIGN(MouseView);
};

// A View that shows a different widget, sets capture on that widget, and
// initiates a nested message-loop when it receives a mouse-press event.
class NestedLoopCaptureView : public View {
 public:
  explicit NestedLoopCaptureView(Widget* widget) : widget_(widget) {}
  virtual ~NestedLoopCaptureView() {}

 private:
  // Overridden from View:
  virtual bool OnMousePressed(const ui::MouseEvent& event) OVERRIDE {
    // Start a nested loop.
    widget_->Show();
    widget_->SetCapture(widget_->GetContentsView());
    EXPECT_TRUE(widget_->HasCapture());

    base::MessageLoopForUI* loop = base::MessageLoopForUI::current();
    base::MessageLoop::ScopedNestableTaskAllower allow(loop);

    base::RunLoop run_loop;
    run_loop.Run();
    return true;
  }

  Widget* widget_;

  DISALLOW_COPY_AND_ASSIGN(NestedLoopCaptureView);
};

}  // namespace

class WidgetTestInteractive : public WidgetTest {
 public:
  WidgetTestInteractive() {}
  virtual ~WidgetTestInteractive() {}

  virtual void SetUp() OVERRIDE {
    gfx::GLSurface::InitializeOneOffForTests();
    ui::RegisterPathProvider();
    base::FilePath ui_test_pak_path;
    ASSERT_TRUE(PathService::Get(ui::UI_TEST_PAK, &ui_test_pak_path));
    ui::ResourceBundle::InitSharedInstanceWithPakPath(ui_test_pak_path);
    WidgetTest::SetUp();
  }

 protected:
  static void ShowQuickMenuImmediately(
      TouchSelectionControllerImpl* controller) {
    DCHECK(controller);
    if (controller->context_menu_timer_.IsRunning()) {
      controller->context_menu_timer_.Stop();
// TODO(tapted): Enable this when porting ui/views/touchui to Mac.
#if !defined(OS_MACOSX)
      controller->ContextMenuTimerFired();
#endif
    }
  }

  static bool IsQuickMenuVisible(TouchSelectionControllerImpl* controller) {
    DCHECK(controller);
    return controller->context_menu_ && controller->context_menu_->visible();
  }
};

#if defined(OS_WIN)
// Tests whether activation and focus change works correctly in Windows.
// We test the following:-
// 1. If the active aura window is correctly set when a top level widget is
//    created.
// 2. If the active aura window in widget 1 created above, is set to NULL when
//    another top level widget is created and focused.
// 3. On focusing the native platform window for widget 1, the active aura
//    window for widget 1 should be set and that for widget 2 should reset.
// TODO(ananta): Discuss with erg on how to write this test for linux x11 aura.
TEST_F(WidgetTestInteractive, DesktopNativeWidgetAuraActivationAndFocusTest) {
  // Create widget 1 and expect the active window to be its window.
  View* contents_view1 = new View;
  contents_view1->SetFocusable(true);
  Widget widget1;
  Widget::InitParams init_params =
      CreateParams(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  init_params.bounds = gfx::Rect(0, 0, 200, 200);
  init_params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  init_params.native_widget = new DesktopNativeWidgetAura(&widget1);
  widget1.Init(init_params);
  widget1.SetContentsView(contents_view1);
  widget1.Show();
  aura::Window* root_window1= widget1.GetNativeView()->GetRootWindow();
  contents_view1->RequestFocus();

  EXPECT_TRUE(root_window1 != NULL);
  aura::client::ActivationClient* activation_client1 =
      aura::client::GetActivationClient(root_window1);
  EXPECT_TRUE(activation_client1 != NULL);
  EXPECT_EQ(activation_client1->GetActiveWindow(), widget1.GetNativeView());

  // Create widget 2 and expect the active window to be its window.
  View* contents_view2 = new View;
  Widget widget2;
  Widget::InitParams init_params2 =
      CreateParams(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  init_params2.bounds = gfx::Rect(0, 0, 200, 200);
  init_params2.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  init_params2.native_widget = new DesktopNativeWidgetAura(&widget2);
  widget2.Init(init_params2);
  widget2.SetContentsView(contents_view2);
  widget2.Show();
  aura::Window* root_window2 = widget2.GetNativeView()->GetRootWindow();
  contents_view2->RequestFocus();
  ::SetActiveWindow(
      root_window2->GetHost()->GetAcceleratedWidget());

  aura::client::ActivationClient* activation_client2 =
      aura::client::GetActivationClient(root_window2);
  EXPECT_TRUE(activation_client2 != NULL);
  EXPECT_EQ(activation_client2->GetActiveWindow(), widget2.GetNativeView());
  EXPECT_EQ(activation_client1->GetActiveWindow(),
            reinterpret_cast<aura::Window*>(NULL));

  // Now set focus back to widget 1 and expect the active window to be its
  // window.
  contents_view1->RequestFocus();
  ::SetActiveWindow(
      root_window1->GetHost()->GetAcceleratedWidget());
  EXPECT_EQ(activation_client2->GetActiveWindow(),
            reinterpret_cast<aura::Window*>(NULL));
  EXPECT_EQ(activation_client1->GetActiveWindow(), widget1.GetNativeView());
}
#endif  // defined(OS_WIN)

TEST_F(WidgetTestInteractive, CaptureAutoReset) {
  Widget* toplevel = CreateTopLevelFramelessPlatformWidget();
  View* container = new View;
  toplevel->SetContentsView(container);

  EXPECT_FALSE(toplevel->HasCapture());
  toplevel->SetCapture(NULL);
  EXPECT_TRUE(toplevel->HasCapture());

  // By default, mouse release removes capture.
  gfx::Point click_location(45, 15);
  ui::MouseEvent release(ui::ET_MOUSE_RELEASED, click_location, click_location,
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  toplevel->OnMouseEvent(&release);
  EXPECT_FALSE(toplevel->HasCapture());

  // Now a mouse release shouldn't remove capture.
  toplevel->set_auto_release_capture(false);
  toplevel->SetCapture(NULL);
  EXPECT_TRUE(toplevel->HasCapture());
  toplevel->OnMouseEvent(&release);
  EXPECT_TRUE(toplevel->HasCapture());
  toplevel->ReleaseCapture();
  EXPECT_FALSE(toplevel->HasCapture());

  toplevel->Close();
  RunPendingMessages();
}

TEST_F(WidgetTestInteractive, ResetCaptureOnGestureEnd) {
  Widget* toplevel = CreateTopLevelFramelessPlatformWidget();
  View* container = new View;
  toplevel->SetContentsView(container);

  View* gesture = new GestureCaptureView;
  gesture->SetBounds(0, 0, 30, 30);
  container->AddChildView(gesture);

  MouseView* mouse = new MouseView;
  mouse->SetBounds(30, 0, 30, 30);
  container->AddChildView(mouse);

  toplevel->SetSize(gfx::Size(100, 100));
  toplevel->Show();

  // Start a gesture on |gesture|.
  ui::GestureEvent tap_down(15,
                            15,
                            0,
                            base::TimeDelta(),
                            ui::GestureEventDetails(ui::ET_GESTURE_TAP_DOWN));
  ui::GestureEvent end(15,
                       15,
                       0,
                       base::TimeDelta(),
                       ui::GestureEventDetails(ui::ET_GESTURE_END));
  toplevel->OnGestureEvent(&tap_down);

  // Now try to click on |mouse|. Since |gesture| will have capture, |mouse|
  // will not receive the event.
  gfx::Point click_location(45, 15);

  ui::MouseEvent press(ui::ET_MOUSE_PRESSED, click_location, click_location,
                       ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::ET_MOUSE_RELEASED, click_location, click_location,
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);

  EXPECT_TRUE(toplevel->HasCapture());

  toplevel->OnMouseEvent(&press);
  toplevel->OnMouseEvent(&release);
  EXPECT_EQ(0, mouse->pressed());

  EXPECT_FALSE(toplevel->HasCapture());

  // The end of the gesture should release the capture, and pressing on |mouse|
  // should now reach |mouse|.
  toplevel->OnGestureEvent(&end);
  toplevel->OnMouseEvent(&press);
  toplevel->OnMouseEvent(&release);
  EXPECT_EQ(1, mouse->pressed());

  toplevel->Close();
  RunPendingMessages();
}

// Checks that if a mouse-press triggers a capture on a different widget (which
// consumes the mouse-release event), then the target of the press does not have
// capture.
TEST_F(WidgetTestInteractive, DisableCaptureWidgetFromMousePress) {
  // The test creates two widgets: |first| and |second|.
  // The View in |first| makes |second| visible, sets capture on it, and starts
  // a nested loop (like a menu does). The View in |second| terminates the
  // nested loop and closes the widget.
  // The test sends a mouse-press event to |first|, and posts a task to send a
  // release event to |second|, to make sure that the release event is
  // dispatched after the nested loop starts.

  Widget* first = CreateTopLevelFramelessPlatformWidget();
  Widget* second = CreateTopLevelFramelessPlatformWidget();

  View* container = new NestedLoopCaptureView(second);
  first->SetContentsView(container);

  second->SetContentsView(new ExitLoopOnRelease());

  first->SetSize(gfx::Size(100, 100));
  first->Show();

  gfx::Point location(20, 20);
  base::MessageLoop::current()->PostTask(FROM_HERE,
      base::Bind(&Widget::OnMouseEvent,
                 base::Unretained(second),
                 base::Owned(new ui::MouseEvent(ui::ET_MOUSE_RELEASED,
                                                location,
                                                location,
                                                ui::EF_LEFT_MOUSE_BUTTON,
                                                ui::EF_LEFT_MOUSE_BUTTON))));
  ui::MouseEvent press(ui::ET_MOUSE_PRESSED, location, location,
                       ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  first->OnMouseEvent(&press);
  EXPECT_FALSE(first->HasCapture());
  first->Close();
  RunPendingMessages();
}

// Tests some grab/ungrab events.
// TODO(estade): can this be enabled now that this is an interactive ui test?
TEST_F(WidgetTestInteractive, DISABLED_GrabUngrab) {
  Widget* toplevel = CreateTopLevelPlatformWidget();
  Widget* child1 = CreateChildNativeWidgetWithParent(toplevel);
  Widget* child2 = CreateChildNativeWidgetWithParent(toplevel);

  toplevel->SetBounds(gfx::Rect(0, 0, 500, 500));

  child1->SetBounds(gfx::Rect(10, 10, 300, 300));
  View* view = new MouseView();
  view->SetBounds(0, 0, 300, 300);
  child1->GetRootView()->AddChildView(view);

  child2->SetBounds(gfx::Rect(200, 10, 200, 200));
  view = new MouseView();
  view->SetBounds(0, 0, 200, 200);
  child2->GetRootView()->AddChildView(view);

  toplevel->Show();
  RunPendingMessages();

  // Click on child1
  gfx::Point p1(45, 45);
  ui::MouseEvent pressed(ui::ET_MOUSE_PRESSED, p1, p1,
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  toplevel->OnMouseEvent(&pressed);

  EXPECT_TRUE(toplevel->HasCapture());
  EXPECT_TRUE(child1->HasCapture());
  EXPECT_FALSE(child2->HasCapture());

  ui::MouseEvent released(ui::ET_MOUSE_RELEASED, p1, p1,
                          ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  toplevel->OnMouseEvent(&released);

  EXPECT_FALSE(toplevel->HasCapture());
  EXPECT_FALSE(child1->HasCapture());
  EXPECT_FALSE(child2->HasCapture());

  RunPendingMessages();

  // Click on child2
  gfx::Point p2(315, 45);
  ui::MouseEvent pressed2(ui::ET_MOUSE_PRESSED, p2, p2,
                          ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  toplevel->OnMouseEvent(&pressed2);
  EXPECT_TRUE(pressed2.handled());
  EXPECT_TRUE(toplevel->HasCapture());
  EXPECT_TRUE(child2->HasCapture());
  EXPECT_FALSE(child1->HasCapture());

  ui::MouseEvent released2(ui::ET_MOUSE_RELEASED, p2, p2,
                           ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  toplevel->OnMouseEvent(&released2);
  EXPECT_FALSE(toplevel->HasCapture());
  EXPECT_FALSE(child1->HasCapture());
  EXPECT_FALSE(child2->HasCapture());

  toplevel->CloseNow();
}

// Tests mouse move outside of the window into the "resize controller" and back
// will still generate an OnMouseEntered and OnMouseExited event..
TEST_F(WidgetTestInteractive, CheckResizeControllerEvents) {
  Widget* toplevel = CreateTopLevelPlatformWidget();

  toplevel->SetBounds(gfx::Rect(0, 0, 100, 100));

  MouseView* view = new MouseView();
  view->SetBounds(90, 90, 10, 10);
  toplevel->GetRootView()->AddChildView(view);

  toplevel->Show();
  RunPendingMessages();

  // Move to an outside position.
  gfx::Point p1(200, 200);
  ui::MouseEvent moved_out(ui::ET_MOUSE_MOVED, p1, p1, ui::EF_NONE,
                           ui::EF_NONE);
  toplevel->OnMouseEvent(&moved_out);
  EXPECT_EQ(0, view->EnteredCalls());
  EXPECT_EQ(0, view->ExitedCalls());

  // Move onto the active view.
  gfx::Point p2(95, 95);
  ui::MouseEvent moved_over(ui::ET_MOUSE_MOVED, p2, p2, ui::EF_NONE,
                            ui::EF_NONE);
  toplevel->OnMouseEvent(&moved_over);
  EXPECT_EQ(1, view->EnteredCalls());
  EXPECT_EQ(0, view->ExitedCalls());

  // Move onto the outer resizing border.
  gfx::Point p3(102, 95);
  ui::MouseEvent moved_resizer(ui::ET_MOUSE_MOVED, p3, p3, ui::EF_NONE,
                               ui::EF_NONE);
  toplevel->OnMouseEvent(&moved_resizer);
  EXPECT_EQ(0, view->EnteredCalls());
  EXPECT_EQ(1, view->ExitedCalls());

  // Move onto the view again.
  toplevel->OnMouseEvent(&moved_over);
  EXPECT_EQ(1, view->EnteredCalls());
  EXPECT_EQ(0, view->ExitedCalls());

  RunPendingMessages();

  toplevel->CloseNow();
}

// Test view focus restoration when a widget is deactivated and re-activated.
TEST_F(WidgetTestInteractive, ViewFocusOnWidgetActivationChanges) {
  Widget* widget1 = CreateTopLevelPlatformWidget();
  View* view1 = new View;
  view1->SetFocusable(true);
  widget1->GetContentsView()->AddChildView(view1);

  Widget* widget2 = CreateTopLevelPlatformWidget();
  View* view2a = new View;
  View* view2b = new View;
  view2a->SetFocusable(true);
  view2b->SetFocusable(true);
  widget2->GetContentsView()->AddChildView(view2a);
  widget2->GetContentsView()->AddChildView(view2b);

  widget1->Show();
  EXPECT_TRUE(widget1->IsActive());
  view1->RequestFocus();
  EXPECT_EQ(view1, widget1->GetFocusManager()->GetFocusedView());

  widget2->Show();
  EXPECT_TRUE(widget2->IsActive());
  EXPECT_FALSE(widget1->IsActive());
  EXPECT_EQ(NULL, widget1->GetFocusManager()->GetFocusedView());
  view2a->RequestFocus();
  EXPECT_EQ(view2a, widget2->GetFocusManager()->GetFocusedView());
  view2b->RequestFocus();
  EXPECT_EQ(view2b, widget2->GetFocusManager()->GetFocusedView());

  widget1->Activate();
  EXPECT_TRUE(widget1->IsActive());
  EXPECT_EQ(view1, widget1->GetFocusManager()->GetFocusedView());
  EXPECT_FALSE(widget2->IsActive());
  EXPECT_EQ(NULL, widget2->GetFocusManager()->GetFocusedView());

  widget2->Activate();
  EXPECT_TRUE(widget2->IsActive());
  EXPECT_EQ(view2b, widget2->GetFocusManager()->GetFocusedView());
  EXPECT_FALSE(widget1->IsActive());
  EXPECT_EQ(NULL, widget1->GetFocusManager()->GetFocusedView());

  widget1->CloseNow();
  widget2->CloseNow();
}

#if defined(OS_WIN)

// Test view focus retention when a widget's HWND is disabled and re-enabled.
TEST_F(WidgetTestInteractive, ViewFocusOnHWNDEnabledChanges) {
  Widget* widget = CreateTopLevelFramelessPlatformWidget();
  widget->SetContentsView(new View);
  for (size_t i = 0; i < 2; ++i) {
    widget->GetContentsView()->AddChildView(new View);
    widget->GetContentsView()->child_at(i)->SetFocusable(true);
  }

  widget->Show();
  const HWND hwnd = HWNDForWidget(widget);
  EXPECT_TRUE(::IsWindow(hwnd));
  EXPECT_TRUE(::IsWindowEnabled(hwnd));
  EXPECT_EQ(hwnd, ::GetActiveWindow());

  for (int i = 0; i < widget->GetContentsView()->child_count(); ++i) {
    SCOPED_TRACE(base::StringPrintf("Child view %d", i));
    View* view = widget->GetContentsView()->child_at(i);

    view->RequestFocus();
    EXPECT_EQ(view, widget->GetFocusManager()->GetFocusedView());
    EXPECT_FALSE(::EnableWindow(hwnd, FALSE));
    EXPECT_FALSE(::IsWindowEnabled(hwnd));

    // Oddly, disabling the HWND leaves it active with the focus unchanged.
    EXPECT_EQ(hwnd, ::GetActiveWindow());
    EXPECT_TRUE(widget->IsActive());
    EXPECT_EQ(view, widget->GetFocusManager()->GetFocusedView());

    EXPECT_TRUE(::EnableWindow(hwnd, TRUE));
    EXPECT_TRUE(::IsWindowEnabled(hwnd));
    EXPECT_EQ(hwnd, ::GetActiveWindow());
    EXPECT_TRUE(widget->IsActive());
    EXPECT_EQ(view, widget->GetFocusManager()->GetFocusedView());
  }

  widget->CloseNow();
}

// This class subclasses the Widget class to listen for activation change
// notifications and provides accessors to return information as to whether
// the widget is active. We need this to ensure that users of the widget
// class activate the widget only when the underlying window becomes really
// active. Previously we would activate the widget in the WM_NCACTIVATE
// message which is incorrect because APIs like FlashWindowEx flash the
// window caption by sending fake WM_NCACTIVATE messages.
class WidgetActivationTest : public Widget {
 public:
  WidgetActivationTest()
      : active_(false) {}

  virtual ~WidgetActivationTest() {}

  virtual void OnNativeWidgetActivationChanged(bool active) OVERRIDE {
    active_ = active;
  }

  bool active() const { return active_; }

 private:
  bool active_;

  DISALLOW_COPY_AND_ASSIGN(WidgetActivationTest);
};

// Tests whether the widget only becomes active when the underlying window
// is really active.
TEST_F(WidgetTestInteractive, WidgetNotActivatedOnFakeActivationMessages) {
  WidgetActivationTest widget1;
  Widget::InitParams init_params =
      CreateParams(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  init_params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  init_params.native_widget = new DesktopNativeWidgetAura(&widget1);
  init_params.bounds = gfx::Rect(0, 0, 200, 200);
  widget1.Init(init_params);
  widget1.Show();
  EXPECT_EQ(true, widget1.active());

  WidgetActivationTest widget2;
  init_params.native_widget = new DesktopNativeWidgetAura(&widget2);
  widget2.Init(init_params);
  widget2.Show();
  EXPECT_EQ(true, widget2.active());
  EXPECT_EQ(false, widget1.active());

  HWND win32_native_window1 = HWNDForWidget(&widget1);
  EXPECT_TRUE(::IsWindow(win32_native_window1));

  ::SendMessage(win32_native_window1, WM_NCACTIVATE, 1, 0);
  EXPECT_EQ(false, widget1.active());
  EXPECT_EQ(true, widget2.active());

  ::SetActiveWindow(win32_native_window1);
  EXPECT_EQ(true, widget1.active());
  EXPECT_EQ(false, widget2.active());
}
#endif  // defined(OS_WIN)

#if !defined(OS_CHROMEOS)
// Provides functionality to create a window modal dialog.
class ModalDialogDelegate : public DialogDelegateView {
 public:
  explicit ModalDialogDelegate(ui::ModalType type) : type_(type) {}
  virtual ~ModalDialogDelegate() {}

  // WidgetDelegate overrides.
  virtual ui::ModalType GetModalType() const OVERRIDE {
    return type_;
  }

 private:
  ui::ModalType type_;

  DISALLOW_COPY_AND_ASSIGN(ModalDialogDelegate);
};

// Tests whether the focused window is set correctly when a modal window is
// created and destroyed. When it is destroyed it should focus the owner window.
TEST_F(WidgetTestInteractive, WindowModalWindowDestroyedActivationTest) {
  TestWidgetFocusChangeListener focus_listener;
  WidgetFocusManager::GetInstance()->AddFocusChangeListener(&focus_listener);
  const std::vector<NativeViewPair>& focus_changes =
      focus_listener.focus_changes();

  // Create a top level widget.
  Widget top_level_widget;
  Widget::InitParams init_params =
      CreateParams(Widget::InitParams::TYPE_WINDOW);
  init_params.show_state = ui::SHOW_STATE_NORMAL;
  gfx::Rect initial_bounds(0, 0, 500, 500);
  init_params.bounds = initial_bounds;
  init_params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  init_params.native_widget =
      new PlatformDesktopNativeWidget(&top_level_widget);
  top_level_widget.Init(init_params);
  top_level_widget.Show();

  gfx::NativeView top_level_native_view = top_level_widget.GetNativeView();
  EXPECT_EQ(1u, focus_changes.size());
  EXPECT_EQ(NativeViewPair(NULL, top_level_native_view), focus_changes[0]);

  // Create a modal dialog.
  // This instance will be destroyed when the dialog is destroyed.
  ModalDialogDelegate* dialog_delegate =
      new ModalDialogDelegate(ui::MODAL_TYPE_WINDOW);

  Widget* modal_dialog_widget = views::DialogDelegate::CreateDialogWidget(
      dialog_delegate, NULL, top_level_widget.GetNativeView());
  modal_dialog_widget->SetBounds(gfx::Rect(100, 100, 200, 200));
  modal_dialog_widget->Show();

  gfx::NativeView modal_native_view = modal_dialog_widget->GetNativeView();
  EXPECT_EQ(3u, focus_changes.size());
  EXPECT_EQ(NativeViewPair(top_level_native_view, modal_native_view),
            focus_changes[1]);
  EXPECT_EQ(NativeViewPair(top_level_native_view, modal_native_view),
            focus_changes[2]);

  modal_dialog_widget->CloseNow();

  EXPECT_EQ(5u, focus_changes.size());
  EXPECT_EQ(NativeViewPair(modal_native_view, top_level_native_view),
            focus_changes[3]);
  EXPECT_EQ(NativeViewPair(modal_native_view, top_level_native_view),
            focus_changes[4]);

  top_level_widget.CloseNow();
  WidgetFocusManager::GetInstance()->RemoveFocusChangeListener(&focus_listener);
}

// Test that when opening a system-modal window, capture is released.
TEST_F(WidgetTestInteractive, SystemModalWindowReleasesCapture) {
  TestWidgetFocusChangeListener focus_listener;
  WidgetFocusManager::GetInstance()->AddFocusChangeListener(&focus_listener);

  // Create a top level widget.
  Widget top_level_widget;
  Widget::InitParams init_params =
      CreateParams(Widget::InitParams::TYPE_WINDOW);
  init_params.show_state = ui::SHOW_STATE_NORMAL;
  gfx::Rect initial_bounds(0, 0, 500, 500);
  init_params.bounds = initial_bounds;
  init_params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  init_params.native_widget =
      new PlatformDesktopNativeWidget(&top_level_widget);
  top_level_widget.Init(init_params);
  top_level_widget.Show();

  EXPECT_EQ(top_level_widget.GetNativeView(),
            focus_listener.focus_changes().back().second);;

  EXPECT_FALSE(top_level_widget.HasCapture());
  top_level_widget.SetCapture(NULL);
  EXPECT_TRUE(top_level_widget.HasCapture());

  // Create a modal dialog.
  ModalDialogDelegate* dialog_delegate =
      new ModalDialogDelegate(ui::MODAL_TYPE_SYSTEM);

  Widget* modal_dialog_widget = views::DialogDelegate::CreateDialogWidget(
      dialog_delegate, NULL, top_level_widget.GetNativeView());
  modal_dialog_widget->SetBounds(gfx::Rect(100, 100, 200, 200));
  modal_dialog_widget->Show();

  EXPECT_FALSE(top_level_widget.HasCapture());

  modal_dialog_widget->CloseNow();
  top_level_widget.CloseNow();
  WidgetFocusManager::GetInstance()->RemoveFocusChangeListener(&focus_listener);
}

#endif  // !defined(OS_CHROMEOS)

TEST_F(WidgetTestInteractive, CanActivateFlagIsHonored) {
  Widget widget;
  Widget::InitParams init_params =
      CreateParams(Widget::InitParams::TYPE_WINDOW);
  init_params.bounds = gfx::Rect(0, 0, 200, 200);
  init_params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  init_params.activatable = Widget::InitParams::ACTIVATABLE_NO;
#if !defined(OS_CHROMEOS)
  init_params.native_widget = new PlatformDesktopNativeWidget(&widget);
#endif  // !defined(OS_CHROMEOS)
  widget.Init(init_params);

  widget.Show();
  EXPECT_FALSE(widget.IsActive());
}

// Test that touch selection quick menu is not activated when opened.
TEST_F(WidgetTestInteractive, TouchSelectionQuickMenuIsNotActivated) {
  CommandLine::ForCurrentProcess()->AppendSwitch(switches::kEnableTouchEditing);
#if defined(OS_WIN)
  views_delegate().set_use_desktop_native_widgets(true);
#endif  // !defined(OS_WIN)

  Widget widget;
  Widget::InitParams init_params =
      CreateParams(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  init_params.bounds = gfx::Rect(0, 0, 200, 200);
  init_params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  widget.Init(init_params);

  Textfield* textfield = new Textfield;
  textfield->SetBounds(0, 0, 200, 20);
  textfield->SetText(base::ASCIIToUTF16("some text"));
  widget.GetRootView()->AddChildView(textfield);

  widget.Show();
  textfield->RequestFocus();
  textfield->SelectAll(true);
  TextfieldTestApi textfield_test_api(textfield);

  RunPendingMessages();

  ui::test::EventGenerator generator(widget.GetNativeWindow());
  generator.GestureTapAt(gfx::Point(10, 10));
  ShowQuickMenuImmediately(static_cast<TouchSelectionControllerImpl*>(
      textfield_test_api.touch_selection_controller()));

  EXPECT_TRUE(textfield->HasFocus());
  EXPECT_TRUE(widget.IsActive());
  EXPECT_TRUE(IsQuickMenuVisible(static_cast<TouchSelectionControllerImpl*>(
      textfield_test_api.touch_selection_controller())));
}

TEST_F(WidgetTestInteractive, DisableViewDoesNotActivateWidget) {
#if defined(OS_WIN)
  views_delegate().set_use_desktop_native_widgets(true);
#endif  // !defined(OS_WIN)

  // Create first widget and view, activate the widget, and focus the view.
  Widget widget1;
  Widget::InitParams params1 = CreateParams(Widget::InitParams::TYPE_POPUP);
  params1.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params1.activatable = Widget::InitParams::ACTIVATABLE_YES;
  widget1.Init(params1);

  View* view1 = new View();
  view1->SetFocusable(true);
  widget1.GetRootView()->AddChildView(view1);

  widget1.Activate();
  EXPECT_TRUE(widget1.IsActive());

  FocusManager* focus_manager1 = widget1.GetFocusManager();
  ASSERT_TRUE(focus_manager1);
  focus_manager1->SetFocusedView(view1);
  EXPECT_EQ(view1, focus_manager1->GetFocusedView());

  // Create second widget and view, activate the widget, and focus the view.
  Widget widget2;
  Widget::InitParams params2 = CreateParams(Widget::InitParams::TYPE_POPUP);
  params2.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params2.activatable = Widget::InitParams::ACTIVATABLE_YES;
  widget2.Init(params2);

  View* view2 = new View();
  view2->SetFocusable(true);
  widget2.GetRootView()->AddChildView(view2);

  widget2.Activate();
  EXPECT_TRUE(widget2.IsActive());
  EXPECT_FALSE(widget1.IsActive());

  FocusManager* focus_manager2 = widget2.GetFocusManager();
  ASSERT_TRUE(focus_manager2);
  focus_manager2->SetFocusedView(view2);
  EXPECT_EQ(view2, focus_manager2->GetFocusedView());

  // Disable the first view and make sure it loses focus, but its widget is not
  // activated.
  view1->SetEnabled(false);
  EXPECT_NE(view1, focus_manager1->GetFocusedView());
  EXPECT_FALSE(widget1.IsActive());
  EXPECT_TRUE(widget2.IsActive());
}

namespace {

// Used to veirfy OnMouseCaptureLost() has been invoked.
class CaptureLostTrackingWidget : public Widget {
 public:
  CaptureLostTrackingWidget() : got_capture_lost_(false) {}
  virtual ~CaptureLostTrackingWidget() {}

  bool GetAndClearGotCaptureLost() {
    bool value = got_capture_lost_;
    got_capture_lost_ = false;
    return value;
  }

  // Widget:
  virtual void OnMouseCaptureLost() OVERRIDE {
    got_capture_lost_ = true;
    Widget::OnMouseCaptureLost();
  }

 private:
  bool got_capture_lost_;

  DISALLOW_COPY_AND_ASSIGN(CaptureLostTrackingWidget);
};

}  // namespace

class WidgetCaptureTest : public ViewsTestBase {
 public:
  WidgetCaptureTest() {
  }

  virtual ~WidgetCaptureTest() {
  }

  virtual void SetUp() OVERRIDE {
    gfx::GLSurface::InitializeOneOffForTests();
    ui::RegisterPathProvider();
    base::FilePath ui_test_pak_path;
    ASSERT_TRUE(PathService::Get(ui::UI_TEST_PAK, &ui_test_pak_path));
    ui::ResourceBundle::InitSharedInstanceWithPakPath(ui_test_pak_path);
    ViewsTestBase::SetUp();
  }

  // Verifies Widget::SetCapture() results in updating native capture along with
  // invoking the right Widget function.
  void TestCapture(bool use_desktop_native_widget) {
    CaptureLostTrackingWidget widget1;
    Widget::InitParams params1 =
        CreateParams(views::Widget::InitParams::TYPE_WINDOW);
    params1.native_widget = CreateNativeWidget(use_desktop_native_widget,
                                               &widget1);
    params1.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
    widget1.Init(params1);
    widget1.Show();

    CaptureLostTrackingWidget widget2;
    Widget::InitParams params2 =
        CreateParams(views::Widget::InitParams::TYPE_WINDOW);
    params2.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
    params2.native_widget = CreateNativeWidget(use_desktop_native_widget,
                                               &widget2);
    widget2.Init(params2);
    widget2.Show();

    // Set capture to widget2 and verity it gets it.
    widget2.SetCapture(widget2.GetRootView());
    EXPECT_FALSE(widget1.HasCapture());
    EXPECT_TRUE(widget2.HasCapture());
    EXPECT_FALSE(widget1.GetAndClearGotCaptureLost());
    EXPECT_FALSE(widget2.GetAndClearGotCaptureLost());

    // Set capture to widget1 and verify it gets it.
    widget1.SetCapture(widget1.GetRootView());
    EXPECT_TRUE(widget1.HasCapture());
    EXPECT_FALSE(widget2.HasCapture());
    EXPECT_FALSE(widget1.GetAndClearGotCaptureLost());
    EXPECT_TRUE(widget2.GetAndClearGotCaptureLost());

    // Release and verify no one has it.
    widget1.ReleaseCapture();
    EXPECT_FALSE(widget1.HasCapture());
    EXPECT_FALSE(widget2.HasCapture());
    EXPECT_TRUE(widget1.GetAndClearGotCaptureLost());
    EXPECT_FALSE(widget2.GetAndClearGotCaptureLost());
  }

  NativeWidget* CreateNativeWidget(bool create_desktop_native_widget,
                                   Widget* widget) {
#if !defined(OS_CHROMEOS)
    if (create_desktop_native_widget)
      return new PlatformDesktopNativeWidget(widget);
#endif
    return NULL;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(WidgetCaptureTest);
};

// See description in TestCapture().
TEST_F(WidgetCaptureTest, Capture) {
  TestCapture(false);
}

#if !defined(OS_CHROMEOS)
// See description in TestCapture(). Creates DesktopNativeWidget.
TEST_F(WidgetCaptureTest, CaptureDesktopNativeWidget) {
  TestCapture(true);
}
#endif

// Test that no state is set if capture fails.
TEST_F(WidgetCaptureTest, FailedCaptureRequestIsNoop) {
  Widget widget;
  Widget::InitParams params =
      CreateParams(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.bounds = gfx::Rect(400, 400);
  widget.Init(params);

  MouseView* mouse_view1 = new MouseView;
  MouseView* mouse_view2 = new MouseView;
  View* contents_view = new View;
  contents_view->AddChildView(mouse_view1);
  contents_view->AddChildView(mouse_view2);
  widget.SetContentsView(contents_view);

  mouse_view1->SetBounds(0, 0, 200, 400);
  mouse_view2->SetBounds(200, 0, 200, 400);

  // Setting capture should fail because |widget| is not visible.
  widget.SetCapture(mouse_view1);
  EXPECT_FALSE(widget.HasCapture());

  widget.Show();
  ui::test::EventGenerator generator(GetContext(), widget.GetNativeWindow());
  generator.set_current_location(gfx::Point(300, 10));
  generator.PressLeftButton();

  EXPECT_FALSE(mouse_view1->pressed());
  EXPECT_TRUE(mouse_view2->pressed());
}

#if !defined(OS_CHROMEOS) && !defined(OS_WIN)
// Test that a synthetic mouse exit is sent to the widget which was handling
// mouse events when a different widget grabs capture.
// TODO(pkotwicz): Make test pass on CrOS and Windows.
TEST_F(WidgetCaptureTest, MouseExitOnCaptureGrab) {
  Widget widget1;
  Widget::InitParams params1 =
      CreateParams(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params1.native_widget = CreateNativeWidget(true, &widget1);
  params1.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  widget1.Init(params1);
  MouseView* mouse_view1 = new MouseView;
  widget1.SetContentsView(mouse_view1);
  widget1.Show();
  widget1.SetBounds(gfx::Rect(300, 300));

  Widget widget2;
  Widget::InitParams params2 =
      CreateParams(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params2.native_widget = CreateNativeWidget(true, &widget2);
  params2.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  widget2.Init(params2);
  widget2.Show();
  widget2.SetBounds(gfx::Rect(400, 0, 300, 300));

  ui::test::EventGenerator generator(widget1.GetNativeWindow());
  generator.set_current_location(gfx::Point(100, 100));
  generator.MoveMouseBy(0, 0);

  EXPECT_EQ(1, mouse_view1->EnteredCalls());
  EXPECT_EQ(0, mouse_view1->ExitedCalls());

  widget2.SetCapture(NULL);
  EXPECT_EQ(0, mouse_view1->EnteredCalls());
  // Grabbing native capture on Windows generates a ui::ET_MOUSE_EXITED event
  // in addition to the one generated by Chrome.
  EXPECT_LT(0, mouse_view1->ExitedCalls());
}
#endif  // !defined(OS_CHROMEOS)

namespace {

// Widget observer which grabs capture when the widget is activated.
class CaptureOnActivationObserver : public WidgetObserver {
 public:
  CaptureOnActivationObserver() {
  }
  virtual ~CaptureOnActivationObserver() {
  }

  // WidgetObserver:
  virtual void OnWidgetActivationChanged(Widget* widget, bool active) OVERRIDE {
    if (active)
      widget->SetCapture(NULL);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CaptureOnActivationObserver);
};

}  // namespace

// Test that setting capture on widget activation of a non-toplevel widget
// (e.g. a bubble on Linux) succeeds.
TEST_F(WidgetCaptureTest, SetCaptureToNonToplevel) {
  Widget toplevel;
  Widget::InitParams toplevel_params =
      CreateParams(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  toplevel_params.native_widget = CreateNativeWidget(true, &toplevel);
  toplevel_params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  toplevel.Init(toplevel_params);
  toplevel.Show();

  Widget* child = new Widget;
  Widget::InitParams child_params =
      CreateParams(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  child_params.parent = toplevel.GetNativeView();
  child_params.context = toplevel.GetNativeWindow();
  child->Init(child_params);

  CaptureOnActivationObserver observer;
  child->AddObserver(&observer);
  child->Show();

  EXPECT_TRUE(child->HasCapture());
}


#if defined(OS_WIN)
namespace {

// Used to verify OnMouseEvent() has been invoked.
class MouseEventTrackingWidget : public Widget {
 public:
  MouseEventTrackingWidget() : got_mouse_event_(false) {}
  virtual ~MouseEventTrackingWidget() {}

  bool GetAndClearGotMouseEvent() {
    bool value = got_mouse_event_;
    got_mouse_event_ = false;
    return value;
  }

  // Widget:
  virtual void OnMouseEvent(ui::MouseEvent* event) OVERRIDE {
    got_mouse_event_ = true;
    Widget::OnMouseEvent(event);
  }

 private:
  bool got_mouse_event_;

  DISALLOW_COPY_AND_ASSIGN(MouseEventTrackingWidget);
};

}  // namespace

// Verifies if a mouse event is received on a widget that doesn't have capture
// on Windows that it is correctly processed by the widget that doesn't have
// capture. This behavior is not desired on OSes other than Windows.
TEST_F(WidgetCaptureTest, MouseEventDispatchedToRightWindow) {
  MouseEventTrackingWidget widget1;
  Widget::InitParams params1 =
      CreateParams(views::Widget::InitParams::TYPE_WINDOW);
  params1.native_widget = new DesktopNativeWidgetAura(&widget1);
  params1.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  widget1.Init(params1);
  widget1.Show();

  MouseEventTrackingWidget widget2;
  Widget::InitParams params2 =
      CreateParams(views::Widget::InitParams::TYPE_WINDOW);
  params2.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params2.native_widget = new DesktopNativeWidgetAura(&widget2);
  widget2.Init(params2);
  widget2.Show();

  // Set capture to widget2 and verity it gets it.
  widget2.SetCapture(widget2.GetRootView());
  EXPECT_FALSE(widget1.HasCapture());
  EXPECT_TRUE(widget2.HasCapture());

  widget1.GetAndClearGotMouseEvent();
  widget2.GetAndClearGotMouseEvent();
  // Send a mouse event to the RootWindow associated with |widget1|. Even though
  // |widget2| has capture, |widget1| should still get the event.
  ui::MouseEvent mouse_event(ui::ET_MOUSE_EXITED, gfx::Point(), gfx::Point(),
                             ui::EF_NONE, ui::EF_NONE);
  ui::EventDispatchDetails details = widget1.GetNativeWindow()->
      GetHost()->event_processor()->OnEventFromSource(&mouse_event);
  ASSERT_FALSE(details.dispatcher_destroyed);
  EXPECT_TRUE(widget1.GetAndClearGotMouseEvent());
  EXPECT_FALSE(widget2.GetAndClearGotMouseEvent());
}
#endif  // defined(OS_WIN)

}  // namespace test
}  // namespace views