package com.abocha.byplanes

import android.bluetooth.*
import com.google.androidgamesdk.GameActivity
import java.io.InputStream
import java.io.OutputStream
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean

class BluetoothManager(private val activity: GameActivity) {

    companion object {
        val SERVICE_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-1234567890ab")
        const val SERVICE_NAME = "ByPlanes"
    }

    private val btAdapter: BluetoothAdapter? = BluetoothAdapter.getDefaultAdapter()
    private var serverSocket: BluetoothServerSocket? = null
    private var connectedSocket: BluetoothSocket? = null
    private var inputStream: InputStream? = null
    private var outputStream: OutputStream? = null
    private val running = AtomicBoolean(false)

    fun startAdvertising() {
        Thread {
            try {
                serverSocket = btAdapter?.listenUsingRfcommWithServiceRecord(SERVICE_NAME, SERVICE_UUID)
                val socket = serverSocket?.accept(30000) // 30 sec timeout
                socket?.let { onConnected(it) }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }.start()
    }

    fun startScanning() {
        Thread {
            try {
                val bonded = btAdapter?.bondedDevices ?: return@Thread
                for (device in bonded) {
                    try {
                        val socket = device.createRfcommSocketToServiceRecord(SERVICE_UUID)
                        socket.connect()
                        onConnected(socket)
                        return@Thread
                    } catch (e: Exception) {
                        // try next device
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }.start()
    }

    private fun onConnected(socket: BluetoothSocket) {
        connectedSocket = socket
        inputStream = socket.inputStream
        outputStream = socket.outputStream
        running.set(true)
        startReceiving()
    }

    fun sendPacket(data: ByteArray) {
        try { outputStream?.write(data) } catch (e: Exception) { e.printStackTrace() }
    }

    fun isConnected(): Boolean = connectedSocket?.isConnected == true && running.get()

    fun disconnect() {
        running.set(false)
        try { connectedSocket?.close() } catch (e: Exception) {}
        connectedSocket = null
        inputStream = null
        outputStream = null
    }

    private fun startReceiving() {
        Thread {
            val buf = ByteArray(64)
            while (running.get()) {
                try {
                    val n = inputStream?.read(buf) ?: break
                    if (n > 0) onPacketReceived(buf.copyOf(n), n)
                } catch (e: Exception) {
                    running.set(false)
                    break
                }
            }
        }.start()
    }

    private external fun onPacketReceived(data: ByteArray, len: Int)
}
