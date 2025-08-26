
const g_floats = new Float32Array(32*24);


function ironbow(value, minVal, maxVal)
{
    let t;
    
    if (maxVal - minVal > 1)
        t = (value - minVal) / (maxVal - minVal);
    else
        t = (value - minVal);

    let r = 0, g = 0, b = 0;

    if (t < 0.25) {              // Blue to Purple
        r = 128.0 * (t / 0.25);
        g = 0;
        b = 255.0;
    } else if (t < 0.5) {        // Purple to Red
        r = 128.0 + 127.0 * ((t - 0.25) / 0.25);
        g = 0;
        b = 255.0 - 255.0 * ((t - 0.25) / 0.25);
    } else if (t < 0.75) {       // Red to Orange
        r = 255.0;
        g = 128.0 * ((t - 0.5) / 0.25);
        b = 0;
    } else {                     // Orange to Yellow/White
        r = 255.0;
        g = 128.0 + 127.0 * ((t - 0.75) / 0.25);
        b = 127.0 * ((t - 0.75) / 0.25);
    }

    // Clamp to [0..255] and integer
    r = Math.max(0, Math.min(255, Math.round(r)));
    g = Math.max(0, Math.min(255, Math.round(g)));
    b = Math.max(0, Math.min(255, Math.round(b)));

    return [r, g, b]; // array [R,G,B]
}

// width has to be multiple of 4 bytes
function drawBMPBase64(frameData, width, height, elId)
{
    const bpp            = 3;
    const BMP_HEADER_LEN = 54;
    const pix_count      = width * height;
    const dataSize       = pix_count*bpp; 
    const fileSize       = BMP_HEADER_LEN + dataSize;

    // zeroed during creation
    const outBuffer = new Uint8Array(fileSize);

    // --- BMP Header (14 bytes) ---
    outBuffer[0] = 0x42; // 'B'
    outBuffer[1] = 0x4D; // 'M'
    outBuffer[2] =  fileSize & 0xFF;
    outBuffer[3] = (fileSize >> 8) & 0xFF;
    outBuffer[4] = (fileSize >> 16) & 0xFF;
    outBuffer[5] = (fileSize >> 24) & 0xFF;
    outBuffer[10] = BMP_HEADER_LEN; // pixel data offset

    // --- DIB Header (BITMAPINFOHEADER, 40 bytes) ---
    outBuffer[14] = 40; // header size
    outBuffer[18] =  width & 0xFF;
    outBuffer[19] = (width >> 8) & 0xFF;
    outBuffer[20] = (width >> 16) & 0xFF;
    outBuffer[21] = (width >> 24) & 0xFF;
    outBuffer[22] =  height & 0xFF;
    outBuffer[23] = (height >> 8) & 0xFF;
    outBuffer[24] = (height >> 16) & 0xFF;
    outBuffer[25] = (height >> 24) & 0xFF;
    outBuffer[26] = 1;     // planes
    outBuffer[28] = 24;    // bits per pixel (24-bit RGB)
    outBuffer[30] = 0;     // compression
    outBuffer[31] = 0;     // compression
    outBuffer[32] = 0;     // compression
    outBuffer[33] = 0;     // compression
    outBuffer[34] =  dataSize & 0xFF;
    outBuffer[35] = (dataSize >> 8) & 0xFF;
    outBuffer[36] = (dataSize >> 16) & 0xFF;
    outBuffer[37] = (dataSize >> 24) & 0xFF;

    // Reinterpret as Float32Array
    const floats = new Float32Array(frameData.buffer, frameData.byteOffset, frameData.byteLength / 4);
    
    // save array to global copy
    if (elId == 'overlay-stream') g_floats.set(floats);

    //--Find min max--------------------------------------
    let fMin =  Infinity;
    let fMax = -Infinity;

    for (let i = 0; i < pix_count; i++) {
        if (floats[i] < fMin) fMin = floats[i];
        if (floats[i] > fMax) fMax = floats[i];
    }

    let i = 0;
    for (let y = 0; y < height; y++)
    {
        for (let x = 0; x < width; x++)
        {
            let srcInd = y * width + (width-1 - x);     // mirror horizontally

            const [r, g, b] = ironbow(floats[srcInd], fMin, fMax);

            let indOut = BMP_HEADER_LEN + i*3;

            // imgData.data is Uint8ClampedArray of length width x height x 4
            outBuffer[indOut + 0] = b;
            outBuffer[indOut + 1] = g;
            outBuffer[indOut + 2] = r;
            i++;
        }
    }

    function uint8ToBase64(uint8) {
        let binary = "";
        for (let i = 0; i < uint8.length; i++) {
            binary += String.fromCharCode(uint8[i]);
        }
        return btoa(binary);
    }

    const img = $(elId);
    img.src   = "data:image/bmp;base64," + uint8ToBase64(outBuffer);
}


function indexOfSubarray(haystack, needle)
{
    if (needle.length === 0) return -1;

    for (let i=0; i <= haystack.length - needle.length; i++)
    {
        let match = true;
        for (let j=0; j < needle.length; j++)
        {
            if (haystack[i + j] !== needle[j]) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return -1;
}

/*
HTTP/1.1 200 OK
Content-Type: multipart/x-mixed-replace; boundary=frame

--frame
Content-Type: image/jpeg
Content-Length: 12345

[binary JPEG data for frame 1]

--frame
Content-Type: image/jpeg
Content-Length: 12087
*/

let controller = null;

async function fetchMultipartBinary(url)
{
    controller = new AbortController();

    try
    {
        const response = await fetch(url, { signal: controller.signal });

        if (!response.ok)   throw new Error("HTTP error " + response.status);
        if (!response.body) throw new Error("ReadableStream not supported");

        // Get boundary from headers
        const contentType = response.headers.get("Content-Type") || "";
        const match = contentType.match(/boundary=([^;]+)/i);
        if (!match) throw new Error("No boundary in Content-Type header");
				    
        // encode into Uint8Array
        const boundarySeparator = new TextEncoder().encode("--" + match[1] + "\r\n");

        // get ReadableStreamDefaultReader
        const reader = response.body.getReader();

        let buffer = new Uint8Array(0);

        // helper: concatenate Uint8Arrays
        function concat(a, b) {
            let c = new Uint8Array(a.length + b.length);

            c.set(a, 0);        // (array, offset in c)
            c.set(b, a.length); // (array, offset in c)

            return c;
        }

        const header2dataSeparator = new TextEncoder().encode("\r\n\r\n");

        while (true) {
            // "value" is Uint8Array holding received chunk of data
            // "done" is boolean turining into true when stream is finished
            const { value, done } = await reader.read();
            if (done) break;
            if (!value) continue;

            buffer = concat(buffer, value);

            let indBoundaryStart;
            // Search for boundary
            while ((indBoundaryStart = indexOfSubarray(buffer, boundarySeparator)) !== -1)
            {
                // Extract CHUNK up to boundary into a new array
                const chunk = buffer.slice(0, indBoundaryStart);

                // Delete CHUNK and trailing Boundary marker from the upcomming data buffer
                buffer = buffer.slice(indBoundaryStart + boundarySeparator.length);

                // PARSE THE CHUNK ---------------------------------------------

                // Split headers and body
                const indHeaderEnd = indexOfSubarray(chunk, header2dataSeparator);

                if (indHeaderEnd !== -1) {
                    const headerBytes = chunk.slice(0, indHeaderEnd);
                    // observe trailing "\n\r"
                    const bodyBytes   = chunk.slice(indHeaderEnd + header2dataSeparator.length, chunk.length-2);

                    const headers = new TextDecoder().decode(headerBytes);
                    const match = headers.match(/Content-Length:\s*(\d+)/);
                    const contentLength = match ? parseInt(match[1], 10) : null;
				                
                    // Call user function with binary body
                    if (contentLength == 32*24*4)
                        drawBMPBase64(bodyBytes, 32, 24, 'overlay-stream');
                }
            }
        }
    }
    catch (err)
    {
        if (err.name === "AbortError")
            console.log("Fetch aborted by user");
        else
            console.error("Fetch failed:", err);
    }
}

async function fetchBinary(url)
{
    const response = await fetch(url);

    if (!response.ok)   throw new Error("HTTP error " + response.status);

    const bodyBytes = await response.arrayBuffer(); // wait for full body

    if (bodyBytes.byteLength == 32*24*4)
        drawBMPBase64(new Uint8Array(bodyBytes), 32, 24, 'overlay-stream');
}


const viewOverlay = $('overlay-stream');
const tooltip     = $('tooltip');


// a timer and a global object storing mouse coords so timer cunc can access last known coords
let timer = null;
let mouseEventCopy = {
    clientX: 0,
    clientY: 0,
    pageX:   0,
    pageY:   0
};

function handleMouseMove(e)
{
    const rect = viewOverlay.getBoundingClientRect();

    // Mouse position relative to image
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    // Scale to thermal array resolution
    const scaledX = 31 - Math.floor(x / rect.width  * 32);  // mirror
    const scaledY = 23 - Math.floor(y / rect.height * 24);  // flip

    // Bounds check
    if (scaledX >= 0 && scaledX < 32 &&
        scaledY >= 0 && scaledY < 24)
    {
        const value = g_floats[scaledY * 32 + scaledX];

        //tooltip.textContent   = `(${scaledX},${scaledY}) = ` + (value).toFixed(1) + "\u00B0C";
        tooltip.textContent   = (value).toFixed(1) + "\u00B0C";
        tooltip.style.left    = e.pageX + 12 + "px";
        tooltip.style.top     = e.pageY - 40 + "px";
        tooltip.style.display = "block";
    }
    else {
        tooltip.style.display = "none";
    }
}

viewOverlay.addEventListener("mousemove", (e) => {

    // update coords
    mouseEventCopy.clientX = e.clientX;
    mouseEventCopy.clientY = e.clientY;
    mouseEventCopy.pageX   = e.pageX;
    mouseEventCopy.pageY   = e.pageY;

    handleMouseMove(mouseEventCopy);
});

viewOverlay.addEventListener("mouseenter", (e) => {
    
    // update coords
    mouseEventCopy.clientX = e.clientX;
    mouseEventCopy.clientY = e.clientY;
    mouseEventCopy.pageX   = e.pageX;
    mouseEventCopy.pageY   = e.pageY;
    
    // call every XXX ms
    timer = setInterval(() => handleMouseMove(mouseEventCopy), 1000);
});

viewOverlay.addEventListener("mouseleave", () => {
    tooltip.style.display = "none";

    clearInterval(timer);
    timer = null;
});


// Initialize images
drawBMPBase64(new Uint8Array(32*24*4), 32, 24, 'overlay-stream');   // inits also temperature array
drawBMPBase64(new Uint8Array(32*24*4), 32, 24, 'stream');


const view          = $('stream');

const startStream = () => {
    view.src = `${streamUrl}/stream`;
    //viewOverlay.src = `${streamOverlayUrl}/stream`;

    fetchMultipartBinary(`${streamOverlayUrl}/stream`);

    $('toggle-stream-btn').innerHTML = 'Stop Stream';
}

const stopStream = () => {
    //window.stop();
    //view.src = '';

    const canvas  = document.createElement('canvas');
    canvas.width  = view.naturalWidth;
    canvas.height = view.naturalHeight;
    const ctx = canvas.getContext('2d');
    ctx.drawImage(view, 0, 0, canvas.width, canvas.height);

    view.src = canvas.toDataURL('image/png');

    if (controller) {
        controller.abort(); // stops the fetch
        controller = null;
    }

    $('toggle-stream-btn').innerHTML = 'Start Stream';
}


// Attach actions to buttons
$('capture-image-btn').onclick = () => {
    stopStream();
    view.src        = `${baseHost}/capture2640?_cb=${Date.now()}`;
        
    //viewOverlay.src = `${baseHost}/capture90640?_cb=${Date.now()}`;
    fetchBinary(`${baseHost}/capture90640?_cb=${Date.now()}`);
}

$('toggle-stream-btn').onclick = () => {
    const streamEnabled = $('toggle-stream-btn').innerHTML === 'Stop Stream';
					
    if (streamEnabled)
        stopStream();
    else
        startStream();
}

$('save-still-btn').onclick = () => {
    var canvas = document.createElement("canvas");
					
    canvas.width  = view.width;
    canvas.height = view.height;

    document.body.appendChild(canvas);

    var context = canvas.getContext('2d');

    context.drawImage(view, 0, 0, canvas.width, canvas.height);

    context.globalAlpha = 1.0 - $('opacity-slider').value/100;
        context.drawImage(viewOverlay, 0, 0, canvas.width, canvas.height);
    context.globalAlpha = 1.0;

    try {
        var dataURL = canvas.toDataURL('image/png');
        $('save-still-btn').href = dataURL;
        var d = new Date();
        $('save-still-btn').download = d.getFullYear() + "-" +
                                      ("0" + (d.getMonth() + 1)).slice(-2) + "-" +
                                      ("0" + d.getDate()).slice(-2) + "_" +
                                      ("0" + d.getHours()).slice(-2) +  "h" +
                                      ("0" + d.getMinutes()).slice(-2) +  "m" +
                                      ("0" + d.getSeconds()).slice(-2) + "s.png";
    }
    catch (e) {
        console.error(e);
    }
    canvas.parentNode.removeChild(canvas);
}