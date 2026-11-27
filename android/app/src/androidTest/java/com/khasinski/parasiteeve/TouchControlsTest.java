package com.khasinski.parasiteeve;

import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.RectF;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import java.lang.reflect.Field;

/** Real Android View/JNI lifecycle checks; no runner code enters the release APK. */
public final class TouchControlsTest extends Instrumentation {
    private GameActivity activity;
    private View controls;
    private long down;

    @Override public void onCreate(Bundle args) { super.onCreate(args); start(); }

    private Object field(String name) throws Exception {
        Field field = controls.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.get(controls);
    }
    private void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private void event(int action, int[] ids, float[] x, float[] y) {
        MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[ids.length];
        MotionEvent.PointerCoords[] coordinates = new MotionEvent.PointerCoords[ids.length];
        for (int i = 0; i < ids.length; ++i) {
            properties[i] = new MotionEvent.PointerProperties();
            properties[i].id = ids[i];
            properties[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
            coordinates[i] = new MotionEvent.PointerCoords();
            coordinates[i].x = x[i]; coordinates[i].y = y[i];
            coordinates[i].pressure = 1; coordinates[i].size = 1;
        }
        if (action == MotionEvent.ACTION_DOWN) down = SystemClock.uptimeMillis();
        MotionEvent event = MotionEvent.obtain(down, SystemClock.uptimeMillis(), action,
            ids.length, properties, coordinates, 0, 0, 1, 1, 0, 0,
            android.view.InputDevice.SOURCE_TOUCHSCREEN, 0);
        try { require(controls.dispatchTouchEvent(event), "Touch was not consumed"); }
        finally { event.recycle(); }
    }
    private void checks() throws Exception {
        RectF[] buttons = (RectF[])field("buttons");
        RectF settings = (RectF)field("settings");
        android.view.WindowInsets insets = controls.getRootWindowInsets();
        int top = insets == null ? 0 : insets.getSystemWindowInsetTop();
        int bottom = controls.getHeight() - (insets == null ? 0 : insets.getSystemWindowInsetBottom());
        for (RectF button : buttons) {
            require(button.top >= top && button.bottom <= bottom,
                "A touch button overlaps a system bar");
            require(button.left >= 0 && button.right <= controls.getWidth(),
                "A touch button lies outside the screen");
            require(!RectF.intersects(button, settings), "Settings overlaps a game button");
        }
        require(settings.top >= top && settings.bottom <= bottom, "Settings overlaps a system bar");
        float cx = (Float)field("cx"), cy = (Float)field("cy"), r = (Float)field("radius");
        float bx = buttons[2].centerX(), by = buttons[2].centerY();
        // Distinct, nonconsecutive pointer IDs exercise independent ownership.
        event(MotionEvent.ACTION_DOWN, new int[]{7}, new float[]{cx}, new float[]{cy-r*.7f});
        require((Float)field("axisY") < -.6f, "Stick did not move up");
        event(MotionEvent.ACTION_POINTER_DOWN | (1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),
            new int[]{7,19}, new float[]{cx,bx}, new float[]{cy-r*.7f,by});
        require(((Integer)field("held") & 1) != 0, "Cross was not held with stick");
        event(MotionEvent.ACTION_POINTER_UP | (1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),
            new int[]{7,19}, new float[]{cx,bx}, new float[]{cy-r*.7f,by});
        require((Integer)field("held") == 0 && (Float)field("axisY") < -.6f,
            "Releasing Cross lost the independent stick");
        event(MotionEvent.ACTION_CANCEL, new int[]{7}, new float[]{cx}, new float[]{cy});
        require((Integer)field("stick") == -1 && (Float)field("axisY") == 0,
            "Cancel left the stick held");
        event(MotionEvent.ACTION_DOWN, new int[]{23}, new float[]{bx}, new float[]{by});
        require((Integer)field("held") == 1, "Cross press failed");
        activity.onWindowFocusChanged(false);
        require((Integer)field("held") == 0, "Focus loss left Cross held");
        activity.onWindowFocusChanged(true);
        event(MotionEvent.ACTION_DOWN, new int[]{31}, new float[]{bx}, new float[]{by});
        callActivityOnPause(activity);
        require((Integer)field("held") == 0, "Pause left Cross held");
        callActivityOnResume(activity);
        // Both shoulder triggers can be held and released independently.
        float lx=buttons[5].centerX(), ly=buttons[5].centerY();
        float rx=buttons[7].centerX(), ry=buttons[7].centerY();
        event(MotionEvent.ACTION_DOWN, new int[]{3}, new float[]{lx}, new float[]{ly});
        event(MotionEvent.ACTION_POINTER_DOWN | (1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),
            new int[]{3,8}, new float[]{lx,rx}, new float[]{ly,ry});
        require((Integer)field("triggers") == 3, "Simultaneous L2/R2 failed");
        event(MotionEvent.ACTION_POINTER_UP, new int[]{3,8}, new float[]{lx,rx}, new float[]{ly,ry});
        require((Integer)field("triggers") == 2, "Releasing L2 lost R2");
        event(MotionEvent.ACTION_UP, new int[]{8}, new float[]{rx}, new float[]{ry});
        require((Integer)field("triggers") == 0, "R2 stuck after release");
    }
    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            Intent intent = new Intent(getTargetContext(), GameActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            intent.putExtra("disc", "disc1.cue");
            intent.putExtra("testProfile", "touch-instrumentation");
            activity = (GameActivity)startActivitySync(intent);
            Field view = GameActivity.class.getDeclaredField("touchControls");
            view.setAccessible(true); controls = (View)view.get(activity);
            long deadline = SystemClock.uptimeMillis()+30000;
            boolean[] ready = {false};
            while (!ready[0] && SystemClock.uptimeMillis()<deadline) {
                runOnMainSync(() -> { try { ready[0]=(Integer)field("status")==1; }
                    catch(Exception error) { throw new RuntimeException(error); } });
                SystemClock.sleep(100);
            }
            require(ready[0], "Touch controller was not ready without hardware");
            Throwable[] failure = {null};
            runOnMainSync(() -> { try { checks(); } catch(Throwable error) { failure[0]=error; } });
            if (failure[0]!=null) throw new AssertionError("Touch lifecycle regression", failure[0]);
            result.putString("stream", "PASS: simultaneous stick/buttons, independent release, cancel, focus loss, pause and L2/R2\n");
            finish(android.app.Activity.RESULT_OK, result);
        } catch(Throwable error) {
            result.putString("stream", android.util.Log.getStackTraceString(error));
            finish(android.app.Activity.RESULT_CANCELED, result);
        }
    }
}
