package com.abocha.byplanes

import android.os.Bundle
import android.view.View
import android.view.WindowManager
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.google.androidgamesdk.GameActivity

class MainActivity : GameActivity() {
    companion object {
        init {
            System.loadLibrary("byplanes")
        }
    }

    // GameActivity registers itself as OnApplyWindowInsetsListener on its SurfaceView.
    // Override to consume all insets so the SurfaceView is never shrunk.
    override fun onApplyWindowInsets(v: View, insets: WindowInsetsCompat): WindowInsetsCompat {
        v.setPadding(0, 0, 0, 0)
        return WindowInsetsCompat.CONSUMED
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        window.attributes.layoutInDisplayCutoutMode =
            WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES

        super.onCreate(savedInstanceState)

        // Also consume insets at the content root (FrameLayout parent of SurfaceView)
        // to prevent the parent from applying padding before insets reach the SurfaceView.
        val contentView = findViewById<View>(android.R.id.content)
        ViewCompat.setOnApplyWindowInsetsListener(contentView) { v, _ ->
            v.setPadding(0, 0, 0, 0)
            WindowInsetsCompat.CONSUMED
        }
        contentView.setPadding(0, 0, 0, 0)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemUi()
        }
    }

    private fun hideSystemUi() {
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.hide(
            WindowInsetsCompat.Type.systemBars() or
            WindowInsetsCompat.Type.displayCutout()
        )
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }
}
