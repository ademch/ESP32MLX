

function drawDivToCanvas(divId, canvas)
{
    const div = $(divId);
    if (!div) return;

    const ctx   = canvas.getContext("2d");
    const style = getComputedStyle(div);

    const rect        = div.getBoundingClientRect();
    const rectOverlay = $('overlay-stream').getBoundingClientRect();

    const left = rect.left - rectOverlay.left;
    const top  = rect.top  - rectOverlay.top;

    const width  = rect.width;
    const height = rect.height;

    const paddingTop   = parseFloat(style.paddingTop);
    const paddingLeft  = parseFloat(style.paddingLeft);
    const borderWidth  = parseFloat(style.borderTopWidth);
    const borderRadius = parseFloat(style.borderTopLeftRadius);

    // Draw background with border radius
    ctx.fillStyle = style.backgroundColor;
    ctx.beginPath();
        ctx.moveTo(left + borderRadius,         top);
        ctx.lineTo(left + width - borderRadius, top);
        ctx.quadraticCurveTo(left + width,      top, left + width, top + borderRadius);
        ctx.lineTo(left + width,                top + height - borderRadius);
        ctx.quadraticCurveTo(left + width,      top + height, left + width - borderRadius, top + height);
        ctx.lineTo(left + borderRadius,         top + height);
        ctx.quadraticCurveTo(left,              top + height, left, top + height - borderRadius);
        ctx.lineTo(left,                        top + borderRadius);
        ctx.quadraticCurveTo(left, top,         left + borderRadius, top);
    ctx.closePath();
    ctx.fill();

    // Draw border
    if (borderWidth > 0)
    {
        ctx.strokeStyle = style.borderColor;
        ctx.lineWidth   = borderWidth;
        ctx.stroke();
    }

    // Draw text
    ctx.fillStyle    = style.color;
    ctx.font         = `${style.fontSize} ${style.fontFamily}`;
    ctx.textBaseline = 'top';
    ctx.fillText(div.textContent.trim(), left + paddingLeft + borderWidth, top + paddingTop + borderWidth + 2);
}


$('save-still-btn').onclick = () => {
    var canvas = document.createElement("canvas");

    canvas.width  = view.width;
    canvas.height = view.height;

    document.body.appendChild(canvas);

    var context = canvas.getContext('2d');

    context.drawImage(view, 0, 0, canvas.width, canvas.height);

    context.globalAlpha = $('opacity-slider').value / 100;
        context.drawImage(viewOverlay, 0, 0, canvas.width, canvas.height);
    context.globalAlpha = 1.0;

    drawDivToCanvas('tooltipMin', canvas);
    drawDivToCanvas('tooltipMax', canvas);

    try {
        var dataURL = canvas.toDataURL('image/png');

        const link = document.createElement('a');
        link.href = dataURL;

        var d = new Date();
        link.download = d.getFullYear() + "-" +
                                      ("0" + (d.getMonth() + 1)).slice(-2) + "-" +
                                      ("0" + d.getDate()).slice(-2) + "_" +
                                      ("0" + d.getHours()).slice(-2) + "h" +
                                      ("0" + d.getMinutes()).slice(-2) + "m" +
                                      ("0" + d.getSeconds()).slice(-2) + "s.png";

        document.body.appendChild(link);
            link.click();
        document.body.removeChild(link);
    }
    catch (e) {
        console.error(e);
    }
    canvas.parentNode.removeChild(canvas);
}

function formatFloat32Array(arr, cols = 32, rows = 24)
{
    if (arr.length !== cols * rows) {
        throw new Error(`Array length must be ${cols * rows}, got ${arr.length}`);
    }

    let result = '';
    for (let r = 0; r < rows; r++)
    {
        let rowStr = '';
        for (let c = 0; c < cols; c++)
        {
            let value = arr[r * cols + c];
            
            // Format to 6.2
            rowStr += value.toFixed(2) + "\t";
        }
        result += rowStr + '\n';
    }
    return result;
}



$('save-data-btn').onclick = () => {

    let emiss   = parseFloat($('emissivity').value);
    let ambRefl = parseFloat($('ambReflected').value);

    var strArray = "\"Emissivity\": " + emiss.toFixed(2) + "\n" +
                   "\"AmbientReflection\": " + ambRefl.toFixed(2) + "\n" +
                   formatFloat32Array(g_floats);

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
                                        ("0" + d.getSeconds()).slice(-2) + "s_values.txt";

        document.body.appendChild(link);
            link.click();                   // Trigger download
        document.body.removeChild(link);

        URL.revokeObjectURL(link.href);     // Clean up memory
    }
    catch (e) {
        console.error(e);
    }
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