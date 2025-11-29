

const startCalibration = async () => {

    alert("Aim the device on uniformly heated surface\nCalibration is done against ambient reflected temperature");

    let ambRefl = parseFloat($('ambReflected').value);

    const response = await fetch(`${baseHost}/mlx?var=calibrate&val=${ambRefl}`, {
                                    headers: {
                                        "X-Client-Date": new Date().toString().split(' GMT')[0]  // e.g. "Wed, 20 Aug 2025 02:45:32"
                                    }
                                });

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
    $('calibration_date').value = "just recently";
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



$('toggle-calibrate-save-btn').onclick = async () => {

    const response = await fetch(`${baseHost}/get_offsets90640`);

    if (!response.ok) throw new Error("HTTP error " + response.status);

    const bodyBytes = await response.arrayBuffer(); // wait for full body

    if (bodyBytes.byteLength != 32*24*4)
    {
        console.log("The number of offsets is different from 768");
        return;
    }

    // Reinterpret as Float32Array
    const floats = new Float32Array(bodyBytes);

    var strArray = formatFloat32Array(floats);

    const blob = new Blob([strArray], { type: 'text/plain' });

    try {

        const link = document.createElement('a');
        link.href = URL.createObjectURL(blob);

        var d = new Date();
        link.download = d.getFullYear() + "-" +
                                        ("0" + (d.getMonth() + 1)).slice(-2) + "-" +
                                        ("0" + d.getDate()).slice(-2) + "_" +
                                        ("0" + d.getHours()).slice(-2) + "h" +
                                        ("0" + d.getMinutes()).slice(-2) + "m" +
                                        ("0" + d.getSeconds()).slice(-2) + "s_offsets.txt";

        document.body.appendChild(link);
        link.click();                   // Trigger download
        document.body.removeChild(link);

        URL.revokeObjectURL(link.href);     // Clean up memory
    }
    catch (e) {
        console.error(e);
    }
}


$('toggle-calibrate-default-btn').onclick = async () => {

    if ( !confirm("Are you sure you want to reset to default calibration profile?") ) return;

    try {
        const response = await fetch(`${baseHost}/mlx?var=reset&val=1`);
        
        if (!response.ok) throw new Error("Reseting to default profile failed");

        const text = await response.text();

        alert("Server says: " + text);

        $('calibration_date').value = "A few moments ago"

    }
    catch (err) {
        alert("Error: " + err.message);
    }
}


$('upload-calibration-btn').onclick = async () => {

    let fileInput = $("calibration-fileInput");

    // Check if any file is selected
    if (!fileInput.files || fileInput.files.length === 0) {
        alert("No file selected");
        return;
    }

    const file = fileInput.files[0];
    if (file.size === 0) {
        alert("File is empty");
        return;
    }

    if ( !confirm("Are you sure you want to update calibration profile?") ) return;


    const reader = new FileReader();

    reader.onload = async function(e) {
        const text = e.target.result;

        // Parse numbers from file text
        const numbers = text
            .split(/[\s,]+/)         // split on spaces, commas, tabs, newlines
            .filter(s => s.length)   // remove empty strings
            .map(Number);            // convert to floats

        // Convert floats to raw binary
        const floatArray = new Float32Array(numbers);
        const byteArray  = new Uint8Array(floatArray.buffer);

        try {
            const response = await fetch(`${baseHost}/set_offsets90640`, {
                method: "POST",
                headers: {
                    "X-Client-Date": new Date().toString().split(' GMT')[0]  // e.g. "Wed, 20 Aug 2025 02:45:32"
                },
                body: byteArray
            });
            if (!response.ok) throw new Error("Upload failed");

            const text = await response.text();

            alert("Server says: " + text);

            $('calibration_date').value = "A few moments ago"

        }
        catch (err) {
            alert("Error: " + err.message);
        }
    }

    reader.onerror = function(e) {
        console.error("Error reading file:", e);
    };

    reader.readAsText(file);

}

