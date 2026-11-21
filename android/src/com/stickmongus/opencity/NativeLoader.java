package com.stickmongus.opencity;

import android.app.NativeActivity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;

public class NativeLoader extends NativeActivity {
    static {
        System.loadLibrary("open_city");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        applyImmersiveFullscreen();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveFullscreen();
        }
    }

    @SuppressWarnings("deprecation")
    private void applyImmersiveFullscreen() {
        View decorView = getWindow().getDecorView();
        if (decorView == null) return;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            android.view.WindowInsetsController ctrl = getWindow().getInsetsController();
            if (ctrl != null) {
                ctrl.hide(android.view.WindowInsets.Type.statusBars()
                        | android.view.WindowInsets.Type.navigationBars());
                ctrl.setSystemBarsBehavior(
                    android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            decorView.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
            );
        }
    }
}
