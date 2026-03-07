package com.abocha.byplanes

import android.Manifest
import android.annotation.SuppressLint
import android.app.AlertDialog
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothServerSocket
import android.bluetooth.BluetoothSocket
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.text.InputFilter
import android.text.InputType
import android.widget.ArrayAdapter
import android.widget.EditText
import android.widget.ListView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.app.ActivityCompat
import com.google.androidgamesdk.GameActivity
import java.io.InputStream
import java.io.OutputStream
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean

class BluetoothManager(private val activity: GameActivity) {

    companion object {
        val SERVICE_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-1234567890ab")
        const val SERVICE_NAME = "ByPlanes"
        private const val ROOM_PREFIX = "ByPlanes:"
        private const val REQUEST_BT_PERMISSIONS = 4017

        @Volatile
        private var activeInstance: BluetoothManager? = null

        fun onPermissionsResult(requestCode: Int, grantResults: IntArray) {
            activeInstance?.handlePermissionsResult(requestCode, grantResults)
        }
    }

    private enum class LocalRole {
        Unknown,
        Host,
        Client
    }

    private data class DiscoveredHost(
        val roomName: String,
        val device: BluetoothDevice
    )

    private val btAdapter: BluetoothAdapter? = BluetoothAdapter.getDefaultAdapter()
    private var serverSocket: BluetoothServerSocket? = null
    private var connectedSocket: BluetoothSocket? = null
    private var inputStream: InputStream? = null
    private var outputStream: OutputStream? = null
    private val running = AtomicBoolean(false)

    private var pendingPermissionAction: (() -> Unit)? = null
    private var hostDialog: AlertDialog? = null
    private var joinDialog: AlertDialog? = null
    private var discoveryReceiver: BroadcastReceiver? = null
    private var listAdapter: ArrayAdapter<String>? = null
    private val discoveredHosts = mutableListOf<DiscoveredHost>()
    private var originalAdapterName: String? = null
    private var hostedRoomName: String? = null
    private var localRole: LocalRole = LocalRole.Unknown

    init {
        activeInstance = this
    }

    fun startAdvertising() {
        activity.runOnUiThread {
            ensureReadyForBluetooth { showHostRoomDialog() }
        }
    }

    fun startScanning() {
        activity.runOnUiThread {
            ensureReadyForBluetooth { showJoinDialog() }
        }
    }

    fun sendPacket(data: ByteArray) {
        try {
            outputStream?.write(data)
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    fun isConnected(): Boolean = connectedSocket?.isConnected == true && running.get()

    fun disconnect() {
        running.set(false)
        stopDiscovery()
        dismissDialog(hostDialog)
        dismissDialog(joinDialog)
        hostDialog = null
        joinDialog = null

        try {
            serverSocket?.close()
        } catch (_: Exception) {
        }
        serverSocket = null

        try {
            connectedSocket?.close()
        } catch (_: Exception) {
        }
        connectedSocket = null
        inputStream = null
        outputStream = null
        localRole = LocalRole.Unknown
        restoreAdapterName()
    }

    private fun handlePermissionsResult(requestCode: Int, grantResults: IntArray) {
        if (requestCode != REQUEST_BT_PERMISSIONS) return
        val action = pendingPermissionAction
        pendingPermissionAction = null
        if (grantResults.isNotEmpty() && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
            action?.invoke()
        } else {
            showToast("Bluetooth permissions are required")
        }
    }

    private fun ensureReadyForBluetooth(action: () -> Unit) {
        val adapter = btAdapter
        if (adapter == null) {
            showToast("Bluetooth is not available")
            return
        }
        if (!adapter.isEnabled) {
            activity.startActivity(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            showToast("Enable Bluetooth and try again")
            return
        }
        ensureBluetoothPermissions(action)
    }

    private fun ensureBluetoothPermissions(action: () -> Unit) {
        val missing = getMissingPermissions()
        if (missing.isEmpty()) {
            action()
            return
        }
        pendingPermissionAction = action
        ActivityCompat.requestPermissions(activity, missing.toTypedArray(), REQUEST_BT_PERMISSIONS)
    }

    private fun getMissingPermissions(): List<String> {
        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            listOf(
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_ADVERTISE
            )
        } else {
            listOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

        return permissions.filter {
            ActivityCompat.checkSelfPermission(activity, it) != PackageManager.PERMISSION_GRANTED
        }
    }

    private fun showHostRoomDialog() {
        dismissDialog(hostDialog)

        val input = EditText(activity).apply {
            hint = "Room name"
            inputType = InputType.TYPE_CLASS_TEXT
            filters = arrayOf(InputFilter.LengthFilter(20))
            setText(hostedRoomName ?: "")
            setSelection(text.length)
        }

        hostDialog = AlertDialog.Builder(activity)
            .setTitle("Host room")
            .setView(input)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Host") { _, _ ->
                val roomName = input.text.toString().trim().ifEmpty { "Room" }
                startHostingRoom(roomName)
            }
            .create()
        hostDialog?.show()
    }

    @SuppressLint("MissingPermission")
    private fun startHostingRoom(roomName: String) {
        disconnect()
        hostedRoomName = roomName
        localRole = LocalRole.Host
        requestDiscoverable()
        advertiseRoomName(roomName)
        showToast("Hosting room \"$roomName\"")

        Thread {
            try {
                serverSocket = btAdapter?.listenUsingRfcommWithServiceRecord(
                    "$SERVICE_NAME:$roomName",
                    SERVICE_UUID
                )
                val socket = serverSocket?.accept()
                if (socket != null) {
                    onConnected(socket)
                }
            } catch (e: Exception) {
                if (!isConnected()) {
                    showToast("Hosting stopped")
                }
            }
        }.start()
    }

    private fun showJoinDialog() {
        dismissDialog(joinDialog)
        discoveredHosts.clear()

        val title = TextView(activity).apply {
            text = "Nearby rooms"
            textSize = 18f
            setPadding(24, 16, 24, 12)
        }
        val listView = ListView(activity)
        listAdapter = ArrayAdapter(
            activity,
            android.R.layout.simple_list_item_1,
            mutableListOf("Searching for rooms...")
        )
        listView.adapter = listAdapter
        listView.setOnItemClickListener { _, _, position, _ ->
            if (position !in discoveredHosts.indices) return@setOnItemClickListener
            val host = discoveredHosts[position]
            dismissDialog(joinDialog)
            joinDialog = null
            connectToHost(host)
        }

        val container = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            addView(title)
            addView(
                listView,
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    600
                )
            )
        }

        joinDialog = AlertDialog.Builder(activity)
            .setTitle("Join room")
            .setView(container)
            .setNegativeButton("Cancel") { _, _ -> stopDiscovery() }
            .create()
        joinDialog?.setOnDismissListener { stopDiscovery() }
        joinDialog?.show()

        startDiscovery()
    }

    @SuppressLint("MissingPermission")
    private fun connectToHost(host: DiscoveredHost) {
        disconnect()
        localRole = LocalRole.Client
        showToast("Connecting to \"${host.roomName}\"")

        Thread {
            try {
                btAdapter?.cancelDiscovery()
                val socket = host.device.createRfcommSocketToServiceRecord(SERVICE_UUID)
                socket.connect()
                onConnected(socket)
            } catch (e: Exception) {
                e.printStackTrace()
                showToast("Unable to connect to \"${host.roomName}\"")
            }
        }.start()
    }

    @SuppressLint("MissingPermission")
    private fun onConnected(socket: BluetoothSocket) {
        try {
            serverSocket?.close()
        } catch (_: Exception) {
        }
        serverSocket = null
        stopDiscovery()

        connectedSocket = socket
        inputStream = socket.inputStream
        outputStream = socket.outputStream
        running.set(true)
        restoreAdapterName()
        showToast(roleToastMessage())
        startReceiving()
    }

    private fun roleToastMessage(): String {
        return when (localRole) {
            LocalRole.Host -> "You control the BLUE plane"
            LocalRole.Client -> "You control the RED plane"
            LocalRole.Unknown -> "Connected"
        }
    }

    private fun startReceiving() {
        Thread {
            val buf = ByteArray(64)
            while (running.get()) {
                try {
                    val n = inputStream?.read(buf) ?: break
                    if (n > 0) onPacketReceived(buf.copyOf(n), n)
                } catch (_: Exception) {
                    break
                }
            }
            running.set(false)
        }.start()
    }

    @SuppressLint("MissingPermission")
    private fun startDiscovery() {
        stopDiscovery()
        discoveredHosts.clear()
        listAdapter?.clear()
        listAdapter?.add("Searching for rooms...")
        listAdapter?.notifyDataSetChanged()

        val receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context?, intent: Intent?) {
                when (intent?.action) {
                    BluetoothDevice.ACTION_FOUND -> {
                        val device = intent.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)
                        val name = device?.name ?: return
                        if (!name.startsWith(ROOM_PREFIX)) return

                        val roomName = name.removePrefix(ROOM_PREFIX).ifBlank { "Room" }
                        if (discoveredHosts.any { it.device.address == device.address }) return

                        if (discoveredHosts.isEmpty()) {
                            listAdapter?.clear()
                        }
                        discoveredHosts += DiscoveredHost(roomName, device)
                        listAdapter?.add("$roomName  ${device.address.takeLast(5)}")
                        listAdapter?.notifyDataSetChanged()
                    }

                    BluetoothAdapter.ACTION_DISCOVERY_FINISHED -> {
                        if (discoveredHosts.isEmpty()) {
                            listAdapter?.clear()
                            listAdapter?.add("No rooms found")
                            listAdapter?.notifyDataSetChanged()
                        }
                    }
                }
            }
        }
        discoveryReceiver = receiver

        val filter = IntentFilter().apply {
            addAction(BluetoothDevice.ACTION_FOUND)
            addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            activity.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            activity.registerReceiver(receiver, filter)
        }

        btAdapter?.cancelDiscovery()
        btAdapter?.startDiscovery()
    }

    @SuppressLint("MissingPermission")
    private fun stopDiscovery() {
        try {
            btAdapter?.cancelDiscovery()
        } catch (_: Exception) {
        }

        val receiver = discoveryReceiver
        if (receiver != null) {
            try {
                activity.unregisterReceiver(receiver)
            } catch (_: Exception) {
            }
            discoveryReceiver = null
        }
    }

    @SuppressLint("MissingPermission")
    private fun advertiseRoomName(roomName: String) {
        val adapter = btAdapter ?: return
        if (originalAdapterName == null) {
            originalAdapterName = adapter.name
        }
        hostedRoomName = roomName
        adapter.name = "$ROOM_PREFIX$roomName"
    }

    @SuppressLint("MissingPermission")
    private fun restoreAdapterName() {
        val adapter = btAdapter ?: return
        val original = originalAdapterName
        if (hostedRoomName != null && original != null && adapter.name != original) {
            adapter.name = original
        }
        hostedRoomName = null
    }

    private fun requestDiscoverable() {
        val intent = Intent(BluetoothAdapter.ACTION_REQUEST_DISCOVERABLE).apply {
            putExtra(BluetoothAdapter.EXTRA_DISCOVERABLE_DURATION, 300)
        }
        activity.startActivity(intent)
    }

    private fun dismissDialog(dialog: AlertDialog?) {
        activity.runOnUiThread {
            try {
                dialog?.dismiss()
            } catch (_: Exception) {
            }
        }
    }

    private fun showToast(message: String) {
        activity.runOnUiThread {
            Toast.makeText(activity, message, Toast.LENGTH_SHORT).show()
        }
    }

    private external fun onPacketReceived(data: ByteArray, len: Int)
}
