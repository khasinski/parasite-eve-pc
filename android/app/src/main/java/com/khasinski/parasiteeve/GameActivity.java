package com.khasinski.parasiteeve;

import org.libsdl.app.SDLActivity;
import android.content.pm.ApplicationInfo;
import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;

/** Android loader for the original game modules compiled into libmain.so. */
public class GameActivity extends SDLActivity {
    private TouchControls touchControls;
    private static native void nativeTouchState(int buttons, int x, int y, int triggers);
    private static native int nativeTouchStatus();

    /** Native SDL virtual controller, with independent pointer ownership for the stick. */
    private final class TouchControls extends android.view.View implements Runnable {
        final android.graphics.Paint paint = new android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG);
        final android.util.SparseArray<android.graphics.PointF> fingers = new android.util.SparseArray<>();
        final java.util.HashSet<Integer> settingsFingers = new java.util.HashSet<>();
        final android.graphics.RectF[] buttons = new android.graphics.RectF[10];
        final String[] labels = {"△", "○", "×", "□", "L1", "L2", "R1", "R2", "SELECT", "START"};
        // SDL gamepad north/east/south/west, shoulders, triggers, back/start.
        final int[] bits = {3, 1, 0, 2, 9, -1, 10, -2, 4, 6};
        final android.graphics.RectF settings = new android.graphics.RectF();
        int stick = -1, status, held, triggers;
        float cx, cy, radius, unit, axisX, axisY;
        TouchControls() { super(GameActivity.this); setFocusable(false); for(int i=0;i<buttons.length;i++)buttons[i]=new android.graphics.RectF(); }
        void clear() {
            fingers.clear(); settingsFingers.clear(); stick=-1; held=triggers=0; axisX=axisY=0;
            nativeTouchState(0,0,0,0); invalidate();
        }
        @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); post(this); }
        @Override protected void onDetachedFromWindow() { removeCallbacks(this); clear(); super.onDetachedFromWindow(); }
        @Override public void run() {
            int next=nativeTouchStatus();
            if(next!=status){status=next;clear();}
            invalidate(); postDelayed(this,250);
        }
        void layoutControls() {
            float density=getResources().getDisplayMetrics().density;
            unit=Math.max(40*density,Math.min(64*density,Math.min(getWidth(),getHeight())*.16f));
            float safeLeft=0,safeTop=0,safeRight=0,safeBottom=0;
            android.view.WindowInsets insets=getRootWindowInsets();
            if(insets!=null){
                safeLeft=insets.getSystemWindowInsetLeft();safeTop=insets.getSystemWindowInsetTop();
                safeRight=insets.getSystemWindowInsetRight();safeBottom=insets.getSystemWindowInsetBottom();
                if(android.os.Build.VERSION.SDK_INT>=28 && insets.getDisplayCutout()!=null){
                    android.view.DisplayCutout cutout=insets.getDisplayCutout();
                    safeLeft=Math.max(safeLeft,cutout.getSafeInsetLeft());
                    safeTop=Math.max(safeTop,cutout.getSafeInsetTop());
                    safeRight=Math.max(safeRight,cutout.getSafeInsetRight());
                    safeBottom=Math.max(safeBottom,cutout.getSafeInsetBottom());
                }
            }
            float left=safeLeft+unit*.2f,right=getWidth()-safeRight-unit*.2f;
            float top=safeTop+unit*.2f,bottom=getHeight()-safeBottom-unit*.2f;
            radius=unit;cx=left+radius;cy=bottom-radius;
            float fx=right-unit,fy=bottom-unit,r=unit*.42f,step=unit*.7f;
            float[] xs={fx,fx+step,fx,fx-step},ys={fy-step,fy,fy+step,fy};
            for(int i=0;i<4;i++)buttons[i].set(xs[i]-r,ys[i]-r,xs[i]+r,ys[i]+r);
            buttons[4].set(left,top,left+unit*1.15f,top+unit*.65f);
            buttons[5].set(left+unit*1.3f,top,left+unit*2.45f,top+unit*.65f);
            buttons[6].set(right-unit*2.45f,top,right-unit*1.3f,top+unit*.65f);
            buttons[7].set(right-unit*1.15f,top,right,top+unit*.65f);
            buttons[8].set(left,top+unit*1.8f,left+unit*1.6f,top+unit*2.45f);
            buttons[9].set(right-unit*1.6f,top+unit*.9f,right,top+unit*1.55f);
            settings.set(left,top+unit*.9f,left+unit*1.6f,top+unit*1.55f);
        }
        boolean stickArea(float x,float y){float dx=x-cx,dy=y-cy;return dx*dx+dy*dy<radius*radius*1.25f;}
        boolean controlArea(float x,float y){
            if(status==0)return false;
            if(settings.contains(x,y))return true;
            if(status!=1)return false;
            if(stickArea(x,y))return true;
            for(android.graphics.RectF b:buttons)if(b.contains(x,y))return true;
            return false;
        }
        void publish() {
            held=triggers=0;axisX=axisY=0;
            for(int n=0;n<fingers.size();n++){
                int id=fingers.keyAt(n);android.graphics.PointF point=fingers.valueAt(n);
                if(id==stick){
                    axisX=(point.x-cx)/radius;axisY=(point.y-cy)/radius;
                    float length=(float)Math.hypot(axisX,axisY);
                    if(length>1){axisX/=length;axisY/=length;}
                } else for(int i=0;i<buttons.length;i++)if(buttons[i].contains(point.x,point.y)){
                    if(bits[i]>=0)held|=1<<bits[i];else triggers|=1<<(-bits[i]-1);
                }
            }
            if(status!=1){held=triggers=0;axisX=axisY=0;}
            nativeTouchState(held,Math.round(axisX*32767),Math.round(axisY*32767),triggers);invalidate();
        }
        @Override public boolean onTouchEvent(android.view.MotionEvent event){
            layoutControls();int action=event.getActionMasked(),at=event.getActionIndex();
            if(action==android.view.MotionEvent.ACTION_CANCEL){clear();return true;}
            if(action==android.view.MotionEvent.ACTION_DOWN && !controlArea(event.getX(),event.getY()))return false;
            if(action==android.view.MotionEvent.ACTION_DOWN || action==android.view.MotionEvent.ACTION_POINTER_DOWN){
                int id=event.getPointerId(at);float x=event.getX(at),y=event.getY(at);
                if(controlArea(x,y)){fingers.put(id,new android.graphics.PointF(x,y));if(settings.contains(x,y))settingsFingers.add(id);if(status==1 && stick<0 && stickArea(x,y))stick=id;}
            }
            for(int i=0;i<event.getPointerCount();i++){
                android.graphics.PointF p=fingers.get(event.getPointerId(i));
                if(p!=null){p.x=event.getX(i);p.y=event.getY(i);}
            }
            if(action==android.view.MotionEvent.ACTION_UP || action==android.view.MotionEvent.ACTION_POINTER_UP){
                int id=event.getPointerId(at);android.graphics.PointF p=fingers.get(id);
                if(settingsFingers.remove(id) && p!=null && settings.contains(p.x,p.y)){
                    SDLActivity.onNativeKeyDown(android.view.KeyEvent.KEYCODE_ESCAPE);
                    SDLActivity.onNativeKeyUp(android.view.KeyEvent.KEYCODE_ESCAPE);
                }
                fingers.remove(id);if(id==stick)stick=-1;
            }
            publish();return true;
        }
        void button(android.graphics.Canvas canvas,android.graphics.RectF box,String label,boolean active){
            paint.setColor(active?0xB0608090:0x50303038);canvas.drawRoundRect(box,unit*.16f,unit*.16f,paint);
            paint.setColor(0xC0FFFFFF);paint.setTextAlign(android.graphics.Paint.Align.CENTER);paint.setTextSize(unit*.25f);
            canvas.drawText(label,box.centerX(),box.centerY()-(paint.ascent()+paint.descent())/2,paint);
        }
        @Override protected void onDraw(android.graphics.Canvas canvas){
            layoutControls();if(status==0)return;
            button(canvas,settings,"Settings",false);
            if(status!=1)return;
            paint.setColor(0x40303038);canvas.drawCircle(cx,cy,radius,paint);
            paint.setColor(0x90708090);canvas.drawCircle(cx+axisX*radius*.7f,cy+axisY*radius*.7f,radius*.34f,paint);
            for(int i=0;i<buttons.length;i++)button(canvas,buttons[i],labels[i],bits[i]>=0?(held&(1<<bits[i]))!=0:(triggers&(1<<(-bits[i]-1)))!=0);
        }
    }
    @Override protected void onPause(){if(touchControls!=null)touchControls.clear();super.onPause();}
    @Override public void onWindowFocusChanged(boolean focused){if(!focused && touchControls!=null)touchControls.clear();super.onWindowFocusChanged(focused);}
    @Override protected void main() {
        super.main();
        // Retire before SDL clears mSDLThread: a picker closing at exactly
        // this point can recreate the Surface and start SDL_main a second time.
        nativeCleanupMainThread();
        android.os.Process.killProcess(android.os.Process.myPid());
    }
    private static final int SELECT_DISC = 4101;
    private volatile String selectedDisc;
    // Called from the original native disc-request path, on SDL's game thread.
    public void requestDiscImage(int identity) {
        selectedDisc = null;
        runOnUiThread(() -> {
            if (isFinishing()) { selectedDisc = ""; return; }
            try {
                android.content.Intent request = new android.content.Intent(this, LauncherActivity.class);
                request.putExtra("requiredDisc", identity);
                startActivityForResult(request, SELECT_DISC);
            } catch (RuntimeException error) { selectedDisc = ""; }
        });
    }
    public byte[] pollDiscImage() {
        String path = selectedDisc;
        return path == null ? null : path.getBytes(java.nio.charset.StandardCharsets.UTF_8);
    }
    @Override protected void onActivityResult(int request, int result, android.content.Intent data) {
        super.onActivityResult(request, result, data);
        if (request != SELECT_DISC) return;
        String disc = result == RESULT_OK && data != null ? data.getStringExtra("disc") : null;
        File image = disc != null && new File(disc).getName().equals(disc)
            ? new File(new File(getFilesDir(), "discs"), disc) : null;
        selectedDisc = image != null && image.isFile() ? image.getAbsolutePath() : "";
    }
    @Override protected void onCreate(android.os.Bundle state) {
        super.onCreate(state);
        if(mLayout!=null){
            touchControls=new TouchControls();
            mLayout.addView(touchControls,new android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,android.view.ViewGroup.LayoutParams.MATCH_PARENT));
        }
        if (android.os.Build.VERSION.SDK_INT >= 33)
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::finish);
    }
    @Override public void onBackPressed() { finish(); }
    @Override public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        // SDL consumes keyboard Back as a key event before Activity navigation.
        // Leave controller input with SDL; system/keyboard Back closes the game.
        if (event.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK &&
            !event.isFromSource(android.view.InputDevice.SOURCE_GAMEPAD)) {
            if (event.getAction() == android.view.KeyEvent.ACTION_UP) finish();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
    @Override protected void onDestroy() {
        // Diagnostic commands may return normally instead of the game's exit().
        // Once SDL_main has completed and SDL has cleaned up, retire this private
        // game process too. Never reuse original C globals for a second session.
        Thread gameThread = mSDLThread;
        super.onDestroy();
        if (isFinishing() && (gameThread == null || !gameThread.isAlive()))
            android.os.Process.killProcess(android.os.Process.myPid());
    }
    @Override protected String[] getLibraries() { return new String[] { "main" }; }
    private void configureControllerDevices() {
        // SDL treats Android DPAD keyboards as joysticks. Keep them in its
        // keyboard path without reserving controller port 1 ahead of touch.
        java.util.HashSet<String> controllers=new java.util.HashSet<>(), keyboards=new java.util.HashSet<>();
        for(int id:android.view.InputDevice.getDeviceIds()){
            android.view.InputDevice device=android.view.InputDevice.getDevice(id);
            if(device==null)continue;
            int sources=device.getSources();
            String pair=String.format(java.util.Locale.ROOT,"0x%04x/0x%04x",device.getVendorId(),device.getProductId());
            boolean controller=(sources&android.view.InputDevice.SOURCE_GAMEPAD)==android.view.InputDevice.SOURCE_GAMEPAD ||
                (sources&android.view.InputDevice.SOURCE_JOYSTICK)==android.view.InputDevice.SOURCE_JOYSTICK;
            if(controller)controllers.add(pair);
            else if((sources&android.view.InputDevice.SOURCE_DPAD)==android.view.InputDevice.SOURCE_DPAD)keyboards.add(pair);
        }
        keyboards.removeAll(controllers); // Do not hide a composite hardware controller.
        if(!keyboards.isEmpty())nativeSetenv("SDL_GAMECONTROLLER_IGNORE_DEVICES",String.join(",",keyboards));
    }
    @Override protected String[] getArguments() {
        // Diagnostic CLI admission is confined to development APKs. Other
        // apps must not be able to choose dump destinations in player storage.
        boolean development = (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        File profile = getFilesDir();
        String testProfile = development ? getIntent().getStringExtra("testProfile") : null;
        if (testProfile != null) {
            if (!testProfile.matches("[A-Za-z0-9_-]+"))
                throw new IllegalArgumentException("Invalid diagnostic profile name");
            profile = new File(new File(getFilesDir(), "diagnostics"), testProfile);
            if (!profile.isDirectory() && !profile.mkdirs())
                throw new IllegalStateException("Cannot create diagnostic profile");
        }
        configureControllerDevices();
        nativeSetenv("SDL_ORIENTATIONS", "LandscapeLeft LandscapeRight");
        nativeSetenv("PE_CONFIG_HOME", profile.getAbsolutePath());
        nativeSetenv("HOME", profile.getAbsolutePath());
        String[] arguments = development ? getIntent().getStringArrayExtra("arguments") : null;
        if (arguments != null) {
            for (String argument : arguments) {
                if (argument.equals("--headless") || argument.equals("--headless-gpu")) {
                    nativeSetenv("PSYZ_AUDIO_CLOCK", "video");
                    nativeSetenv("SDL_AUDIO_DRIVER", "dummy");
                }
            }
        }
        if (arguments != null) return arguments;
        String disc = getIntent().getStringExtra("disc");
        File directory = new File(getFilesDir(), "discs");
        if (disc == null || !new File(disc).getName().equals(disc)) return new String[0];
        File selected = new File(directory, disc);
        if (!selected.isFile()) return new String[0];
        ArrayList<String> options = new ArrayList<>();
        options.add("--disc");
        options.add(selected.getAbsolutePath());
        File[] others = directory.listFiles(f -> f.isFile() && f.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".cue") && !f.equals(selected));
        if (others != null && others.length != 0) {
            Arrays.sort(others);
            options.add("--disc2");
            options.add(others[0].getAbsolutePath());
        }
        return options.toArray(new String[0]);
    }
}
