

const startCalibration = async () => {

    alert("Aim the device on uniformly heated surface\nCalibration is done against ambient reflected temperature");

    let ambRefl = parseFloat($('ambReflected').value);

    const response = await fetch(`${baseHost}/mlx?var=calibrate&val=${ambRefl}`);

    if (!response.ok)   throw new Error("HTTP error " + response.status);
    if (!response.body) throw new Error("ReadableStream not supported");

    // get ReadableStreamDefaultReader
    const reader = response.body.getReader();

    while (true) {
        // "value" is Uint8Array holding received chunk of data,
        // "done"  is boolean turining into true when stream is finished
        const { value, done } = await reader.read();
        if (done) break;
        if (!value) continue;

        for (let i = 0; i < value.length; i++) {
            $('calibration-progress').value = value[i];
        }
    }

    $('toggle-calibrate-btn').innerHTML = 'Calibrate';
    $('toggle-calibrate-btn').style.background = '#00AA00';
    $('toggle-calibrate-btn').disabled = true;
}

// Attach actions to buttons

$('toggle-calibrate-btn').onclick = () => {

    if (!confirm("Are you sure you want to start calibration?\nThat will erase previous calibration data"))
        return;

    const streamInProgress = $('toggle-stream-btn').innerHTML === 'Stop Stream';
    if (!streamInProgress) {
        alert("Streaming has to be started first");
        return;
    }

    $('toggle-calibrate-btn').innerHTML = '...';
    $('toggle-calibrate-btn').style.background = '#ff3034';
    $('toggle-calibrate-btn').disabled = true;

    startCalibration();
}
