// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

package com.tencent.ncnnyolox;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.Spinner;

import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;

public class MainActivity extends Activity implements SurfaceHolder.Callback, JNICallbackInterface {
    public static final int REQUEST_CAMERA = 100;

    private final NcnnYolox ncnnyolox = new NcnnYolox(this);

    private boolean isOn = false;
    private boolean isDelegate = false;

    private int facing = 1;

    private int current_model = 0;
    private int current_cpugpu = 0;
    private int current_rate = 0;

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        SurfaceView cameraView = (SurfaceView) findViewById(R.id.cameraview);

        cameraView.getHolder().setFormat(PixelFormat.RGBA_8888);
        cameraView.getHolder().addCallback(this);

        CheckBox checkOnOff = (CheckBox) findViewById(R.id.checkOnOff);
        checkOnOff.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                isOn = isChecked;
                reload();
            }
        });

        CheckBox checkDelegate = (CheckBox) findViewById(R.id.checkDelegate);
        checkDelegate.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                isDelegate = isChecked;
                reload();
            }
        });

        Spinner spinnerModel = (Spinner) findViewById(R.id.spinnerModel);
        spinnerModel.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> arg0, View arg1, int position, long id) {
                if (position != current_model) {
                    current_model = position;
                    reload();
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> arg0) {
            }
        });

        Spinner spinnerCPUGPU = (Spinner) findViewById(R.id.spinnerCPUGPU);
        spinnerCPUGPU.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> arg0, View arg1, int position, long id) {
                if (position != current_cpugpu) {
                    current_cpugpu = position;
                    reload();
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> arg0) {
            }
        });

        Spinner spinnerRate = (Spinner) findViewById(R.id.spinnerRate);
        spinnerRate.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> arg0, View arg1, int position, long id) {
                if (position != current_rate) {
                    current_rate = position;
                    reload();
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> arg0) {
            }
        });

        reload();
    }

    private void reload() {
        boolean ret_init = ncnnyolox.loadModel(getAssets(), current_model, current_cpugpu, current_rate, isOn,
                isDelegate);
        if (!ret_init) {
            Log.e("MainActivity", "ncnnyolox loadModel failed");
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        ncnnyolox.setOutputWindow(holder.getSurface());
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
    }

    @Override
    public void onResume() {
        super.onResume();

        if (ContextCompat.checkSelfPermission(getApplicationContext(),
                Manifest.permission.CAMERA) == PackageManager.PERMISSION_DENIED) {
            ActivityCompat.requestPermissions(this, new String[] { Manifest.permission.CAMERA }, REQUEST_CAMERA);
        }

        ncnnyolox.openCamera(facing);
    }

    @Override
    public void onPause() {
        super.onPause();

        ncnnyolox.closeCamera();
    }

    @Override
    public void callBackEvent(int data) {
        Log.i("MainActivity callback", String.valueOf(data));
    }
}
