package com.drbeef.ioq3quest;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.view.WindowManager;

/**
 * Storage permission gate.
 *
 * The engine reads its data straight off /sdcard/UrTQuest, so it needs all-files access
 * before it starts. That gate cannot live in MainActivity: it extends SDLActivity, whose
 * super.onCreate() spins up the native thread, so the engine would start whether or not the user
 * granted anything. This activity owns the launcher icon instead, and only hands off to
 * MainActivity once access is in place.
 */
public class LauncherActivity extends Activity {

	private static final String TAG = "ioquake3Quest";
	private static final int REQUEST_MANAGE_ALL_FILES = 2296;
	private static final int REQUEST_STORAGE_PERMISSION = 2297;

	// The hand-off can be reached from both onActivityResult and onResume, and starting the
	// engine twice would be bad news.
	private boolean launched = false;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		checkPermissionAndLaunch();
	}

	@Override
	protected void onResume() {
		super.onResume();
		// Backing out of the all-files settings screen does not reliably deliver a result on the
		// Quest, so re-check here too. Without this the user grants access and then sits looking
		// at an apparently dead app until they relaunch it.
		if (!launched && hasStorageAccess()) {
			launchGame();
		}
	}

	private boolean hasStorageAccess() {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
			return Environment.isExternalStorageManager();
		}
		return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
				== PackageManager.PERMISSION_GRANTED;
	}

	private void checkPermissionAndLaunch() {
		if (hasStorageAccess()) {
			launchGame();
			return;
		}

		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
			Log.i(TAG, "Requesting MANAGE_EXTERNAL_STORAGE");
			try {
				Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
				intent.setData(Uri.fromParts("package", getPackageName(), null));
				startActivityForResult(intent, REQUEST_MANAGE_ALL_FILES);
			} catch (Exception e) {
				// Some runtimes do not offer the per-app screen; the general list is the fallback.
				Log.w(TAG, "Per-app all-files screen unavailable: " + e);
				startActivityForResult(
						new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION),
						REQUEST_MANAGE_ALL_FILES);
			}
		} else {
			requestPermissions(new String[]{
					Manifest.permission.READ_EXTERNAL_STORAGE,
					Manifest.permission.WRITE_EXTERNAL_STORAGE
			}, REQUEST_STORAGE_PERMISSION);
		}
	}

	private void launchGame() {
		launched = true;
		Intent intent = new Intent(this, MainActivity.class);
		intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
		startActivity(intent);
		finish();
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		if (requestCode == REQUEST_MANAGE_ALL_FILES && !launched) {
			if (hasStorageAccess()) {
				launchGame();
			} else {
				Log.w(TAG, "Storage access refused, exiting");
				finishAffinity();
			}
		}
	}

	@Override
	public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
		super.onRequestPermissionsResult(requestCode, permissions, results);
		if (requestCode == REQUEST_STORAGE_PERMISSION && !launched) {
			if (hasStorageAccess()) {
				launchGame();
			} else {
				Log.w(TAG, "Storage permission refused, exiting");
				finishAffinity();
			}
		}
	}
}
