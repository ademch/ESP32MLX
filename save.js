

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
        $('save-still-btn').href = dataURL;
        var d = new Date();
        $('save-still-btn').download = d.getFullYear() + "-" +
                                      ("0" + (d.getMonth() + 1)).slice(-2) + "-" +
                                      ("0" + d.getDate()).slice(-2) + "_" +
                                      ("0" + d.getHours()).slice(-2) + "h" +
                                      ("0" + d.getMinutes()).slice(-2) + "m" +
                                      ("0" + d.getSeconds()).slice(-2) + "s.png";
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
        var dataURL = URL.createObjectURL(blob);
        $('save-data-btn').href = dataURL;
        var d = new Date();
        $('save-data-btn').download = d.getFullYear() + "-" +
                                        ("0" + (d.getMonth() + 1)).slice(-2) + "-" +
                                        ("0" + d.getDate()).slice(-2) + "_" +
                                        ("0" + d.getHours()).slice(-2) + "h" +
                                        ("0" + d.getMinutes()).slice(-2) + "m" +
                                        ("0" + d.getSeconds()).slice(-2) + "s.txt";
    }
    catch (e) {
        console.error(e);
    }
}