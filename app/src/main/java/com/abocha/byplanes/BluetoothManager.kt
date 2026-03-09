package com.abocha.byplanes

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
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
import android.location.LocationManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.util.Log
import android.widget.ArrayAdapter
import android.widget.ListView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.app.ActivityCompat
import android.provider.Settings
import com.google.androidgamesdk.GameActivity
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.util.ArrayDeque
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

class BluetoothManager(private val activity: GameActivity) {
    companion object {
        private const val TAG = "BluetoothManager"
        val SERVICE_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-1234567890ab")
        val ROOM_INFO_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-1234567890ac")
        const val SERVICE_NAME = "BIPLANES"
        private const val ROOM_PREFIX = "BIPLANES:"
        private const val REQUEST_BT_PERMISSIONS = 4017
        private const val REQUEST_BT_DISCOVERABLE = 4018
        private const val MATCH_STATE_PACKET_TYPE = 0
        private const val CONTROL_PACKET_TYPE_PAUSE = 1
        private const val CONTROL_PACKET_TYPE_RESUME = 2
        private const val CONTROL_PACKET_TYPE_END_MATCH = 3
        private const val INPUT_PACKET_TYPE = 4

        @Volatile
        private var activeInstance: BluetoothManager? = null

        fun onPermissionsResult(requestCode: Int, grantResults: IntArray) {
            activeInstance?.handlePermissionsResult(requestCode, grantResults)
        }

        fun onActivityResult(requestCode: Int, resultCode: Int) {
            activeInstance?.handleActivityResult(requestCode, resultCode)
        }
    }

    private enum class LocalRole {
        Unknown,
        Host,
        Client
    }

    private enum class SocketMode(val label: String) {
        Insecure("insecure RFCOMM"),
        Secure("secure RFCOMM")
    }

    private data class DiscoveredHost(
        val device: BluetoothDevice,
        var roomName: String,
        var isVerifiedHost: Boolean = false,
        var isChecking: Boolean = false,
        var isBonded: Boolean = false,
        var hasExplicitRoomName: Boolean = false
    )

    private val btAdapter: BluetoothAdapter? = BluetoothAdapter.getDefaultAdapter()
    private var serverSocket: BluetoothServerSocket? = null
    private var roomInfoServerSocket: BluetoothServerSocket? = null
    private var connectedSocket: BluetoothSocket? = null
    private var inputStream: InputStream? = null
    private var outputStream: OutputStream? = null
    private val running = AtomicBoolean(false)
    private val connectionSessionId = AtomicInteger(0)
    private val sendLock = Object()
    private val outputWriteLock = Object()
    private val pendingControlPackets = ArrayDeque<ByteArray>()
    private var pendingMatchPacket: ByteArray? = null
    private val pendingInputPackets = ArrayDeque<ByteArray>()

    private var pendingPermissionAction: (() -> Unit)? = null
    private var pendingDiscoverableAction: (() -> Unit)? = null
    private var hostDialog: AlertDialog? = null
    private var joinDialog: AlertDialog? = null
    private var discoveryReceiver: BroadcastReceiver? = null
    private var listAdapter: ArrayAdapter<String>? = null
    private var joinDebugView: TextView? = null
    private val discoveredHosts = mutableListOf<DiscoveredHost>()
    private val visibleHosts = mutableListOf<DiscoveredHost>()
    private val pendingServiceChecks = mutableMapOf<String, BluetoothDevice>()
    private val pendingRoomNameChecks = mutableSetOf<String>()
    private val rawDevicesSeen = linkedSetOf<String>()
    private var originalAdapterName: String? = null
    private var hostedRoomName: String? = null
    private var localRole: LocalRole = LocalRole.Unknown
    private val mainHandler = Handler(Looper.getMainLooper())
    private val hostingSessionId = AtomicInteger(0)
    private var joinScanRound = 0
    private var joinDiscoveryStartAttempts = 0
    private var joinUuidChecksStarted = 0
    private var joinUuidChecksResolved = 0
    private var joinDiscoveryFinishedCount = 0
    private var joinLastDiscoveryStarted = false
    private var joinLastEvent = "idle"
    private val restartDiscoveryRunnable = Runnable {
        if (joinDialog?.isShowing == true && !isConnected()) {
            btAdapter?.cancelDiscovery()
            joinLastEvent = "auto refresh"
            attemptStartDiscovery("auto refresh", 0)
        }
    }

    init {
        activeInstance = this
    }

    fun startAdvertising() {
        activity.runOnUiThread {
            ensureReadyForBluetooth {
                if (localRole == LocalRole.Host || serverSocket != null || roomInfoServerSocket != null || isConnected()) {
                    disconnect()
                }
                startHostingRoom(resolveHostedRoomName())
            }
        }
    }

    fun startScanning() {
        activity.runOnUiThread {
            ensureReadyForBluetooth(needsLocation = true) {
                ensureLocationServicesEnabled {
                    showJoinDialog()
                }
            }
        }
    }

    @Synchronized
    fun sendPacket(data: ByteArray) {
        if (!running.get() || data.isEmpty()) {
            return
        }

        synchronized(sendLock) {
            enqueueOutboundPacketLocked(data)
            sendLock.notifyAll()
        }
    }

    @Synchronized
    fun sendPacketAndDisconnect(data: ByteArray) {
        if (data.isEmpty()) {
            disconnect()
            return
        }

        val packet = framePacket(data.copyOf())
        val stream = outputStream
        if (!running.get() || stream == null) {
            disconnect()
            return
        }

        Thread({
            try {
                synchronized(outputWriteLock) {
                    stream.write(packet)
                    stream.flush()
                }
            } catch (_: Exception) {
            } finally {
                disconnect()
            }
        }, "BIPLANESBtSendAndDisconnect").start()
    }

    fun isConnected(): Boolean = connectedSocket?.isConnected == true && running.get()

    fun isHostRole(): Boolean = localRole == LocalRole.Host

    fun showCurrentRoleToast() {
        showToast(roleToastMessage())
    }

    fun dismissConnectionDialogs() {
        dismissDialog(hostDialog)
        dismissDialog(joinDialog)
        hostDialog = null
        joinDialog = null
    }

    fun disconnect() {
        hostingSessionId.incrementAndGet()
        connectionSessionId.incrementAndGet()
        running.set(false)
        stopDiscovery()
        pendingDiscoverableAction = null
        dismissDialog(hostDialog)
        dismissDialog(joinDialog)
        hostDialog = null
        joinDialog = null
        joinDebugView = null
        synchronized(sendLock) {
            clearPendingOutboundPacketsLocked()
            sendLock.notifyAll()
        }

        try {
            serverSocket?.close()
        } catch (_: Exception) {
        }
        serverSocket = null
        closeRoomInfoServerSocketQuietly()
        pendingRoomNameChecks.clear()

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

    private fun handleActivityResult(requestCode: Int, resultCode: Int) {
        if (requestCode != REQUEST_BT_DISCOVERABLE) return
        val action = pendingDiscoverableAction
        pendingDiscoverableAction = null
        if (resultCode == Activity.RESULT_CANCELED) {
            if (localRole != LocalRole.Host) {
                restoreAdapterName()
            }
            showToast("Discoverable mode is required to host a room")
            return
        }
        action?.invoke()
    }

    private fun ensureReadyForBluetooth(needsLocation: Boolean = false, action: () -> Unit) {
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
        ensureBluetoothPermissions(needsLocation, action)
    }

    private fun ensureBluetoothPermissions(needsLocation: Boolean, action: () -> Unit) {
        val missing = getMissingPermissions(needsLocation)
        if (missing.isEmpty()) {
            action()
            return
        }
        pendingPermissionAction = action
        ActivityCompat.requestPermissions(activity, missing.toTypedArray(), REQUEST_BT_PERMISSIONS)
    }

    private fun getMissingPermissions(needsLocation: Boolean): List<String> {
        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            buildList {
                addAll(
                    listOf(
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_ADVERTISE
                    )
                )
                if (needsLocation) {
                    add(Manifest.permission.ACCESS_FINE_LOCATION)
                }
            }
        } else {
            listOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

        return permissions.filter {
            ActivityCompat.checkSelfPermission(activity, it) != PackageManager.PERMISSION_GRANTED
        }
    }

    private fun showHostWaitingDialog(hostName: String) {
        dismissDialog(hostDialog)

        hostDialog = AlertDialog.Builder(activity)
            .setTitle("Name of your device: $hostName")
            .setMessage("Connecting...\nWaiting for player to connect")
            .setCancelable(false)
            .setNegativeButton("STOP") { _, _ -> disconnect() }
            .create()
        hostDialog?.show()
    }

    private fun showClientConnectingDialog(hostName: String) {
        dismissDialog(joinDialog)
        joinDialog = AlertDialog.Builder(activity)
            .setTitle(hostName)
            .setMessage("Connecting...")
            .setCancelable(false)
            .setNegativeButton("STOP") { _, _ -> disconnect() }
            .create()
        joinDialog?.show()
    }

    private fun updateConnectionEstablishedDialog() {
        activity.runOnUiThread {
            when (localRole) {
                LocalRole.Host -> {
                    hostDialog?.setTitle("Name of your device: ${hostedRoomName ?: resolveHostedRoomName()}")
                    hostDialog?.setMessage("Connection - match starts soon")
                }

                LocalRole.Client -> {
                    joinDialog?.setTitle("Connection")
                    joinDialog?.setMessage("Connection - match starts soon")
                }

                LocalRole.Unknown -> Unit
            }
        }
    }

    private fun sanitizeDisplayDeviceName(rawName: String?): String? {
        val trimmed = rawName?.trim()?.takeUnless { it.isBlank() } ?: return null
        return if (trimmed.startsWith(ROOM_PREFIX)) null else trimmed
    }

    private fun resolveSystemDeviceName(): String? {
        val resolver = activity.contentResolver
        val candidates = listOf(
            Settings.Global.getString(resolver, "device_name"),
            Settings.Secure.getString(resolver, "device_name")
        )
        return candidates.firstNotNullOfOrNull { sanitizeDisplayDeviceName(it) }
    }

    @SuppressLint("MissingPermission")
    private fun resolveHostedRoomName(): String {
        resolveSystemDeviceName()?.let { return it }
        sanitizeDisplayDeviceName(btAdapter?.name)?.let { return it }
        return Build.MODEL?.trim().takeUnless { it.isNullOrBlank() } ?: "Bluetooth Host"
    }

    @SuppressLint("MissingPermission")
    private fun startHostingRoom(roomName: String, discoverableChecked: Boolean = false) {
        val adapter = btAdapter
        if (!discoverableChecked && adapter != null
            && adapter.scanMode != BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE
        ) {
            requestDiscoverable { startHostingRoom(roomName, discoverableChecked = true) }
            return
        }

        disconnect()
        hostedRoomName = roomName
        localRole = LocalRole.Host
        advertiseRoomName(roomName)
        adapter?.cancelDiscovery()
        showHostWaitingDialog(roomName)
        val sessionId = hostingSessionId.incrementAndGet()

        Thread {
            var lastError: Exception? = null
            try {
                for (mode in listOf(SocketMode.Insecure, SocketMode.Secure)) {
                    if (hostingSessionId.get() != sessionId || localRole != LocalRole.Host) {
                        return@Thread
                    }

                    try {
                        serverSocket = createServerSocket(roomName, mode)
                        Log.d(TAG, "Hosting room \"$roomName\" using ${mode.label}")
                        val socket = serverSocket?.accept()
                        if (socket != null) {
                            onConnected(socket)
                            return@Thread
                        }
                    } catch (e: Exception) {
                        lastError = e
                        Log.e(TAG, "Hosting failed for room \"$roomName\" using ${mode.label}", e)
                        closeServerSocketQuietly()
                    }
                }
            } finally {
                if (!isConnected()) {
                    closeServerSocketQuietly()
                }
            }

            if (hostingSessionId.get() == sessionId && localRole == LocalRole.Host && !isConnected()) {
                showToast(describeBluetoothError("Unable to host room", lastError ?: IllegalStateException("No server socket available")))
                disconnect()
            }
        }.start()
    }

    private fun showJoinDialog() {
        dismissDialog(joinDialog)
        discoveredHosts.clear()
        visibleHosts.clear()
        resetJoinDebugState()

        val title = TextView(activity).apply {
            text = "Nearby devices"
            textSize = 18f
            setPadding(24, 16, 24, 12)
        }

        val listView = ListView(activity)
        val listHeight = (activity.resources.displayMetrics.heightPixels * 0.45f).toInt().coerceAtLeast(420)
        listAdapter = ArrayAdapter(
            activity,
            android.R.layout.simple_list_item_1,
            mutableListOf("Searching for devices...")
        )
        listView.adapter = listAdapter
        listView.setOnItemClickListener { _, _, position, _ ->
            if (position !in visibleHosts.indices) return@setOnItemClickListener
            val host = visibleHosts[position]
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
                    listHeight
                )
            )
        }

        joinDialog = AlertDialog.Builder(activity)
            .setTitle("Join device")
            .setView(container)
            .setNegativeButton("Cancel") { _, _ -> stopDiscovery() }
            .create()
        joinDialog?.setOnDismissListener {
            stopDiscovery()
            joinDebugView = null
        }
        joinDialog?.show()

        startDiscovery()
    }

    @SuppressLint("MissingPermission")
    private fun connectToHost(host: DiscoveredHost) {
        disconnect()
        localRole = LocalRole.Client
        showClientConnectingDialog(host.roomName)

        Thread {
            btAdapter?.cancelDiscovery()
            var lastError: Exception? = null

            for (mode in listOf(SocketMode.Insecure, SocketMode.Secure)) {
                var socket: BluetoothSocket? = null
                try {
                    socket = createClientSocket(host.device, mode)
                    Log.d(TAG, "Connecting to \"${host.roomName}\" using ${mode.label}")
                    socket.connect()
                    onConnected(socket)
                    return@Thread
                } catch (e: Exception) {
                    lastError = e
                    Log.e(TAG, "Unable to connect to host \"${host.roomName}\" using ${mode.label}", e)
                    try {
                        socket?.close()
                    } catch (_: Exception) {
                    }
                }
            }

            if (localRole == LocalRole.Client && !isConnected()) {
                dismissDialog(joinDialog)
                joinDialog = null
                showToast(
                    describeBluetoothError(
                        "Unable to connect to \"${host.roomName}\"",
                        lastError ?: IllegalStateException("Connection failed")
                    )
                )
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
        closeRoomInfoServerSocketQuietly()
        stopDiscovery()

        connectedSocket = socket
        inputStream = socket.inputStream
        outputStream = socket.outputStream
        running.set(true)
        val sessionId = connectionSessionId.incrementAndGet()
        synchronized(sendLock) {
            clearPendingOutboundPacketsLocked()
        }
        restoreAdapterName()
        updateConnectionEstablishedDialog()
        startSending(sessionId)
        startReceiving(sessionId)
    }

    private fun roleToastMessage(): String {
        return when (localRole) {
            LocalRole.Host -> "You control the BLUE plane"
            LocalRole.Client -> "You control the RED plane"
            LocalRole.Unknown -> "Connected"
        }
    }

    private fun startSending(sessionId: Int) {
        Thread({
            while (running.get() && connectionSessionId.get() == sessionId) {
                val packet = synchronized(sendLock) {
                    while (running.get()
                        && connectionSessionId.get() == sessionId
                        && !hasPendingOutboundPacketsLocked()
                    ) {
                        sendLock.wait()
                    }

                    if (!running.get() || connectionSessionId.get() != sessionId) {
                        null
                    } else {
                        pollOutboundPacketLocked()
                    }
                } ?: continue

                try {
                    var missingStream = false
                    synchronized(outputWriteLock) {
                        val stream = outputStream
                        if (stream == null) {
                            missingStream = true
                        } else {
                            stream.write(framePacket(packet))
                        }
                    }
                    if (missingStream) break
                } catch (_: Exception) {
                    break
                }
            }

            if (connectionSessionId.get() == sessionId) {
                running.set(false)
            }
        }, "BIPLANESBtSender").start()
    }

    private fun startReceiving(sessionId: Int) {
        Thread({
            val buf = ByteArray(1024)
            val pending = ByteArrayOutputStream()
            while (running.get() && connectionSessionId.get() == sessionId) {
                try {
                    val n = inputStream?.read(buf) ?: break
                    if (n <= 0) break
                    pending.write(buf, 0, n)
                    val allBytes = pending.toByteArray()
                    var offset = 0

                    while (offset + 2 <= allBytes.size) {
                        val packetLength =
                            ((allBytes[offset].toInt() and 0xFF) shl 8) or
                            (allBytes[offset + 1].toInt() and 0xFF)
                        if (offset + 2 + packetLength > allBytes.size) {
                            break
                        }
                        onPacketReceived(
                            allBytes.copyOfRange(offset + 2, offset + 2 + packetLength),
                            packetLength
                        )
                        offset += 2 + packetLength
                    }

                    pending.reset()
                    if (offset < allBytes.size) {
                        pending.write(allBytes, offset, allBytes.size - offset)
                    }
                } catch (_: Exception) {
                    break
                }
            }
            if (connectionSessionId.get() == sessionId) {
                running.set(false)
            }
        }, "BIPLANESBtReceiver").start()
    }

    private fun framePacket(packet: ByteArray): ByteArray {
        val framed = ByteArray(packet.size + 2)
        framed[0] = ((packet.size ushr 8) and 0xFF).toByte()
        framed[1] = (packet.size and 0xFF).toByte()
        System.arraycopy(packet, 0, framed, 2, packet.size)
        return framed
    }

    private fun enqueueOutboundPacketLocked(packet: ByteArray) {
        when (packet[0].toInt() and 0xFF) {
            MATCH_STATE_PACKET_TYPE -> {
                pendingMatchPacket = packet.copyOf()
            }

            INPUT_PACKET_TYPE -> {
                pendingInputPackets.addLast(packet.copyOf())
            }

            CONTROL_PACKET_TYPE_PAUSE,
            CONTROL_PACKET_TYPE_RESUME,
            CONTROL_PACKET_TYPE_END_MATCH -> {
                pendingControlPackets.addLast(packet.copyOf())
            }

            else -> {
                pendingControlPackets.addLast(packet.copyOf())
            }
        }
    }

    private fun hasPendingOutboundPacketsLocked(): Boolean {
        return pendingControlPackets.isNotEmpty()
            || pendingInputPackets.isNotEmpty()
            || pendingMatchPacket != null
    }

    private fun pollOutboundPacketLocked(): ByteArray? {
        pendingControlPackets.pollFirst()?.let { return it }
        pendingInputPackets.pollFirst()?.let { return it }
        pendingMatchPacket?.let {
            pendingMatchPacket = null
            return it
        }
        return null
    }

    private fun clearPendingOutboundPacketsLocked() {
        pendingControlPackets.clear()
        pendingInputPackets.clear()
        pendingMatchPacket = null
    }

    @SuppressLint("MissingPermission")
    private fun startDiscovery() {
        stopDiscovery()
        discoveredHosts.clear()
        pendingServiceChecks.clear()
        rawDevicesSeen.clear()
        joinScanRound += 1
        joinUuidChecksStarted = 0
        joinUuidChecksResolved = 0
        joinDiscoveryFinishedCount = 0
        joinDiscoveryStartAttempts = 0
        joinLastEvent = "starting discovery"
        refreshDiscoveredList()

        val receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context?, intent: Intent?) {
                when (intent?.action) {
                    BluetoothDevice.ACTION_FOUND -> {
                        val device = getBluetoothDevice(intent) ?: return
                        val advertisedName = intent.getStringExtra(BluetoothDevice.EXTRA_NAME) ?: device.name
                        onDeviceFound(device, advertisedName)
                    }

                    BluetoothDevice.ACTION_UUID -> {
                        val device = getBluetoothDevice(intent) ?: return
                        val uuids = getBluetoothUuids(intent)
                        onDeviceUuidsResolved(device, uuids)
                    }

                    BluetoothAdapter.ACTION_DISCOVERY_STARTED -> {
                        joinLastDiscoveryStarted = true
                        joinLastEvent = "discovery broadcast started"
                        updateJoinDebugView()
                    }

                    BluetoothAdapter.ACTION_DISCOVERY_FINISHED -> {
                        joinDiscoveryFinishedCount += 1
                        joinLastEvent = "discovery finished"
                        updateJoinDebugView()
                        if (joinDialog?.isShowing == true && !isConnected()) {
                            if (visibleHosts.isEmpty()) {
                                listAdapter?.clear()
                                listAdapter?.add("No devices found yet")
                                listAdapter?.notifyDataSetChanged()
                            }
                            mainHandler.removeCallbacks(restartDiscoveryRunnable)
                            mainHandler.postDelayed(restartDiscoveryRunnable, 1200L)
                        }
                    }
                }
            }
        }
        discoveryReceiver = receiver

        val filter = IntentFilter().apply {
            addAction(BluetoothDevice.ACTION_FOUND)
            addAction(BluetoothDevice.ACTION_UUID)
            addAction(BluetoothAdapter.ACTION_DISCOVERY_STARTED)
            addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            activity.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            activity.registerReceiver(receiver, filter)
        }

        seedBondedDevices()
        if (btAdapter?.isDiscovering == true) {
            btAdapter?.cancelDiscovery()
        }
        attemptStartDiscovery("initial start", 0)
    }

    @SuppressLint("MissingPermission")
    private fun stopDiscovery() {
        mainHandler.removeCallbacks(restartDiscoveryRunnable)
        pendingServiceChecks.clear()
        joinLastEvent = "discovery stopped"
        updateJoinDebugView()
        try {
            if (btAdapter?.isDiscovering == true) {
                btAdapter?.cancelDiscovery()
            }
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
        hostedRoomName = roomName
    }

    @SuppressLint("MissingPermission")
    private fun restoreAdapterName() {
        hostedRoomName = null
    }

    @Suppress("DEPRECATION")
    private fun requestDiscoverable(action: () -> Unit) {
        pendingDiscoverableAction = action
        val intent = Intent(BluetoothAdapter.ACTION_REQUEST_DISCOVERABLE).apply {
            putExtra(BluetoothAdapter.EXTRA_DISCOVERABLE_DURATION, 300)
        }
        activity.startActivityForResult(intent, REQUEST_BT_DISCOVERABLE)
    }

    private fun dismissDialog(dialog: AlertDialog?) {
        activity.runOnUiThread {
            try {
                dialog?.dismiss()
            } catch (_: Exception) {
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun onDeviceFound(device: BluetoothDevice, advertisedName: String?) {
        val isNewRawDevice = rawDevicesSeen.add(device.address)
        if (isNewRawDevice) {
            joinLastEvent = "found ${device.address.takeLast(5)}"
            updateJoinDebugView()
        }
        val existing = discoveredHosts.firstOrNull { it.device.address == device.address }
        if (existing == null) {
            addDiscoveredHost(
                device = device,
                advertisedName = advertisedName,
                isVerifiedHost = true,
                isChecking = false,
                hasExplicitRoomName = true
            )
        } else {
            existing.roomName = resolveRoomName(advertisedName ?: device.name)
            existing.isChecking = false
            existing.isVerifiedHost = true
            existing.hasExplicitRoomName = true
            refreshDiscoveredList()
        }
    }

    @SuppressLint("MissingPermission")
    private fun onDeviceUuidsResolved(device: BluetoothDevice, uuids: Array<ParcelUuid>) {
        val existing = discoveredHosts.firstOrNull { it.device.address == device.address }
        if (existing != null) {
            existing.roomName = resolveRoomName(device.name)
            existing.isChecking = false
            existing.isVerifiedHost = true
            existing.hasExplicitRoomName = true
            refreshDiscoveredList()
        }
    }

    @SuppressLint("MissingPermission")
    private fun seedBondedDevices() {
        val adapter = btAdapter ?: return
        val bondedDevices = try {
            adapter.bondedDevices.orEmpty()
        } catch (_: SecurityException) {
            emptySet()
        }

        bondedDevices.forEach { device ->
            val cachedName = device.name
            addDiscoveredHost(
                device = device,
                advertisedName = cachedName,
                isVerifiedHost = true,
                isChecking = false,
                hasExplicitRoomName = true
            )
        }
    }

    @SuppressLint("MissingPermission")
    private fun addDiscoveredHost(
        device: BluetoothDevice,
        advertisedName: String?,
        isVerifiedHost: Boolean,
        isChecking: Boolean,
        hasExplicitRoomName: Boolean
    ): DiscoveredHost {
        val resolvedRoomName = resolveRoomName(advertisedName ?: device.name)
        val existing = discoveredHosts.firstOrNull { it.device.address == device.address }
        if (existing != null) {
            existing.roomName = resolvedRoomName
            existing.isVerifiedHost = true
            existing.isChecking = isChecking
            existing.isBonded = existing.isBonded || device.bondState == BluetoothDevice.BOND_BONDED
            existing.hasExplicitRoomName = true
            refreshDiscoveredList()
            return existing
        } else {
            val host = DiscoveredHost(
                device = device,
                roomName = resolvedRoomName,
                isVerifiedHost = isVerifiedHost,
                isChecking = isChecking,
                isBonded = device.bondState == BluetoothDevice.BOND_BONDED,
                hasExplicitRoomName = hasExplicitRoomName
            )
            discoveredHosts += host
            refreshDiscoveredList()
            return host
        }
    }

    private fun resolveExplicitRoomName(rawName: String?): String? {
        return sanitizeDisplayDeviceName(rawName)
    }

    private fun resolveRoomName(rawName: String?): String {
        return sanitizeDisplayDeviceName(rawName)
            ?: rawName?.removePrefix(ROOM_PREFIX)?.trim()?.takeUnless { it.isBlank() }
            ?: "Bluetooth device"
    }

    private fun refreshDiscoveredList() {
        visibleHosts.clear()
        visibleHosts.addAll(
            discoveredHosts
                .sortedBy { it.roomName.lowercase() }
        )
        listAdapter?.clear()
        if (visibleHosts.isEmpty()) {
            listAdapter?.add("Searching for devices...")
            listAdapter?.notifyDataSetChanged()
            updateJoinDebugView()
            return
        }

        visibleHosts.forEach { host ->
            listAdapter?.add(formatDiscoveredHost(host))
        }
        listAdapter?.notifyDataSetChanged()
        updateJoinDebugView()
    }

    private fun formatDiscoveredHost(host: DiscoveredHost): String {
        return host.roomName
    }

    private fun resetJoinDebugState() {
        rawDevicesSeen.clear()
        joinScanRound = 0
        joinDiscoveryStartAttempts = 0
        joinUuidChecksStarted = 0
        joinUuidChecksResolved = 0
        joinDiscoveryFinishedCount = 0
        joinLastDiscoveryStarted = false
        joinLastEvent = "idle"
    }

    private fun updateJoinDebugView() {
        val verifiedHosts = discoveredHosts.count { it.isVerifiedHost }
        val checkingHosts = discoveredHosts.count { it.isChecking }
        val visibleDevices = discoveredHosts.size
        val pendingChecks = pendingServiceChecks.size
        val bondedVisible = discoveredHosts.count { it.isBonded }
        val adapterEnabled = btAdapter?.isEnabled == true
        val adapterDiscovering = btAdapter?.isDiscovering == true
        val adapterState = bluetoothStateLabel(btAdapter?.state ?: BluetoothAdapter.STATE_OFF)
        val adapterScanMode = bluetoothScanModeLabel(btAdapter?.scanMode ?: BluetoothAdapter.SCAN_MODE_NONE)
        val scanGranted = hasPermission(Manifest.permission.BLUETOOTH_SCAN)
        val connectGranted = hasPermission(Manifest.permission.BLUETOOTH_CONNECT)
        val advertiseGranted = hasPermission(Manifest.permission.BLUETOOTH_ADVERTISE)
        val locationGranted =
            Build.VERSION.SDK_INT < Build.VERSION_CODES.S || hasPermission(Manifest.permission.ACCESS_FINE_LOCATION)
        val locationEnabled = isLocationEnabled()
        val text = buildString {
            append("Debug")
            append('\n')
            append("round=").append(joinScanRound)
            append("  attempts=").append(joinDiscoveryStartAttempts)
            append("  raw=").append(rawDevicesSeen.size)
            append("  visible=").append(visibleDevices)
            append("  bonded=").append(bondedVisible)
            append('\n')
            append("uuid started=").append(joinUuidChecksStarted)
            append("  resolved=").append(joinUuidChecksResolved)
            append("  pending=").append(pendingChecks)
            append('\n')
            append("hosts=").append(verifiedHosts)
            append("  checking=").append(checkingHosts)
            append("  finished=").append(joinDiscoveryFinishedCount)
            append('\n')
            append("bt=").append(if (adapterEnabled) "on" else "off")
            append("  state=").append(adapterState)
            append("  discovering=").append(if (adapterDiscovering) "yes" else "no")
            append('\n')
            append("scanMode=").append(adapterScanMode)
            append("  scanPerm=").append(if (scanGranted) "yes" else "no")
            append("  connectPerm=").append(if (connectGranted) "yes" else "no")
            append('\n')
            append("advPerm=").append(if (advertiseGranted) "yes" else "no")
            append("  locPerm=").append(if (locationGranted) "yes" else "no")
            append("  locOn=").append(if (locationEnabled) "yes" else "no")
            append('\n')
            append("startDiscovery=").append(if (joinLastDiscoveryStarted) "ok" else "fail")
            append("  event=").append(joinLastEvent)
        }

        activity.runOnUiThread {
            joinDebugView?.text = text
        }
    }

    @SuppressLint("MissingPermission")
    private fun attemptStartDiscovery(reason: String, attempt: Int) {
        joinDiscoveryStartAttempts = attempt + 1
        joinLastDiscoveryStarted = false

        val adapter = btAdapter
        if (adapter == null) {
            joinLastEvent = "no adapter"
            updateJoinDebugView()
            return
        }

        if (!adapter.isEnabled || adapter.state != BluetoothAdapter.STATE_ON) {
            joinLastEvent = "adapter not ready (${bluetoothStateLabel(adapter.state)})"
            updateJoinDebugView()
            if (attempt < 3) {
                mainHandler.postDelayed({ attemptStartDiscovery(reason, attempt + 1) }, 800L)
            }
            return
        }

        val started = try {
            adapter.startDiscovery()
        } catch (e: SecurityException) {
            joinLastEvent = "security exception"
            updateJoinDebugView()
            showToast(describeBluetoothError("Discovery failed", e))
            return
        }

        joinLastDiscoveryStarted = started
        joinLastEvent = if (started) {
            "$reason ok"
        } else {
            "$reason false (${bluetoothStateLabel(adapter.state)})"
        }
        updateJoinDebugView()

        if (!started && attempt < 3 && joinDialog?.isShowing == true && !isConnected()) {
            mainHandler.postDelayed({ attemptStartDiscovery(reason, attempt + 1) }, 1200L)
        }
    }

    private fun bluetoothStateLabel(state: Int): String {
        return when (state) {
            BluetoothAdapter.STATE_OFF -> "OFF"
            BluetoothAdapter.STATE_TURNING_OFF -> "TURNING_OFF"
            BluetoothAdapter.STATE_ON -> "ON"
            BluetoothAdapter.STATE_TURNING_ON -> "TURNING_ON"
            else -> state.toString()
        }
    }

    private fun bluetoothScanModeLabel(scanMode: Int): String {
        return when (scanMode) {
            BluetoothAdapter.SCAN_MODE_NONE -> "NONE"
            BluetoothAdapter.SCAN_MODE_CONNECTABLE -> "CONNECTABLE"
            BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE -> "DISCOVERABLE"
            else -> scanMode.toString()
        }
    }

    private fun hasPermission(permission: String): Boolean {
        return ActivityCompat.checkSelfPermission(activity, permission) == PackageManager.PERMISSION_GRANTED
    }

    private fun isLocationEnabled(): Boolean {
        val manager = activity.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
        return manager?.isLocationEnabled ?: true
    }

    private fun ensureLocationServicesEnabled(action: () -> Unit) {
        if (isLocationEnabled()) {
            action()
            return
        }

        showToast("Turn on Location for Bluetooth discovery")
        activity.startActivity(Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS))
    }

    private fun getBluetoothDevice(intent: Intent): BluetoothDevice? {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
        }
    }

    private fun getBluetoothUuids(intent: Intent): Array<ParcelUuid> {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableArrayExtra(BluetoothDevice.EXTRA_UUID, ParcelUuid::class.java)
                ?: emptyArray()
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableArrayExtra(BluetoothDevice.EXTRA_UUID)
                ?.mapNotNull { it as? ParcelUuid }
                ?.toTypedArray()
                ?: emptyArray()
        }
    }

    @SuppressLint("MissingPermission")
    private fun createServerSocket(roomName: String, mode: SocketMode): BluetoothServerSocket? {
        val adapter = btAdapter ?: return null
        val serviceName = "$SERVICE_NAME:$roomName"
        return createServerSocket(serviceName, SERVICE_UUID, mode, adapter)
    }

    @SuppressLint("MissingPermission")
    private fun createServerSocket(
        serviceName: String,
        uuid: UUID,
        mode: SocketMode,
        adapter: BluetoothAdapter?
    ): BluetoothServerSocket? {
        val resolvedAdapter = adapter ?: btAdapter ?: return null
        return when (mode) {
            SocketMode.Insecure -> resolvedAdapter.listenUsingInsecureRfcommWithServiceRecord(serviceName, uuid)
            SocketMode.Secure -> resolvedAdapter.listenUsingRfcommWithServiceRecord(serviceName, uuid)
        }
    }

    @SuppressLint("MissingPermission")
    private fun createClientSocket(device: BluetoothDevice, mode: SocketMode): BluetoothSocket {
        return createClientSocket(device, SERVICE_UUID, mode)
    }

    @SuppressLint("MissingPermission")
    private fun createClientSocket(device: BluetoothDevice, uuid: UUID, mode: SocketMode): BluetoothSocket {
        return when (mode) {
            SocketMode.Insecure -> device.createInsecureRfcommSocketToServiceRecord(uuid)
            SocketMode.Secure -> device.createRfcommSocketToServiceRecord(uuid)
        }
    }

    @SuppressLint("MissingPermission")
    private fun startRoomInfoServer(roomName: String, sessionId: Int) {
        closeRoomInfoServerSocketQuietly()
        Thread {
            try {
                roomInfoServerSocket =
                    createServerSocket("$SERVICE_NAME:$roomName:info", ROOM_INFO_UUID, SocketMode.Insecure, null)
                        ?: createServerSocket("$SERVICE_NAME:$roomName:info", ROOM_INFO_UUID, SocketMode.Secure, null)
                val payload = roomName.toByteArray(Charsets.UTF_8)
                while (hostingSessionId.get() == sessionId && localRole == LocalRole.Host) {
                    val socket = roomInfoServerSocket?.accept() ?: break
                    try {
                        socket.outputStream?.write(payload)
                        socket.outputStream?.flush()
                    } finally {
                        try {
                            socket.close()
                        } catch (_: Exception) {
                        }
                    }
                }
            } catch (_: Exception) {
            } finally {
                closeRoomInfoServerSocketQuietly()
            }
        }.start()
    }

    private fun closeServerSocketQuietly() {
        try {
            serverSocket?.close()
        } catch (_: Exception) {
        }
        serverSocket = null
    }

    private fun closeRoomInfoServerSocketQuietly() {
        try {
            roomInfoServerSocket?.close()
        } catch (_: Exception) {
        }
        roomInfoServerSocket = null
    }

    private fun describeBluetoothError(prefix: String, error: Exception): String {
        val rawMessage = error.message?.trim().orEmpty()
        val reason = when {
            error is SecurityException -> "Bluetooth permission denied"
            rawMessage.contains("socket closed", ignoreCase = true) -> "socket closed"
            rawMessage.contains("read failed", ignoreCase = true) -> "socket I/O failed"
            rawMessage.contains("discovery failed", ignoreCase = true) -> "discovery failed"
            rawMessage.contains("service discovery failed", ignoreCase = true) -> "service discovery failed"
            rawMessage.contains("not supported", ignoreCase = true) -> "Bluetooth not supported by this device"
            rawMessage.isNotBlank() -> rawMessage
            else -> error.javaClass.simpleName.ifBlank { "unknown Bluetooth error" }
        }

        val normalized = reason.replace('\n', ' ').replace('\r', ' ').trim()
        val shortened = if (normalized.length > 72) normalized.take(69).trimEnd() + "..." else normalized
        return "$prefix: $shortened"
    }

    private fun showToast(message: String, duration: Int = Toast.LENGTH_SHORT) {
        activity.runOnUiThread {
            Toast.makeText(activity, message, duration).show()
        }
    }

    private external fun onPacketReceived(data: ByteArray, len: Int)
}
