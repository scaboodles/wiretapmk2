import Foundation
import AVFoundation
import Dispatch

let audioEngine = AVAudioEngine()

if #available(macOS 10.14, *) {
    if(AVCaptureDevice.authorizationStatus(for: .audio) != .authorized) {
        print("not authorized")
        exit(0)
    }
} // else just send it?

let audioInputNode = audioEngine.inputNode
audioInputNode.installTap(onBus: 0, bufferSize: 256, format: audioInputNode.inputFormat(forBus: 0)) { buffer, when in
    for channel in 0..<Int(buffer.format.channelCount) {
        let channelData = buffer.floatChannelData?[channel]
        // print each frame for sample 
        if let channelData = channelData {
            for frame in 0..<Int(buffer.frameLength) {
                let sample = channelData[frame]
                print("\(sample)")
            }
        }
    }
}

do {
    try audioEngine.start()
} catch {
    print("Error starting audio engine: \(error.localizedDescription)")
}

let semaphore = DispatchSemaphore(value: 0)
semaphore.wait()