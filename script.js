
const $ = (id) => document.getElementById(id);

var baseHost         = document.location.origin;
var streamUrl        = baseHost + ':81';
var streamOverlayUrl = baseHost + ':82';


function fetchUrl(url, cb)
{
    fetch(url)
        .then(function(response)
        {
            if (response.status !== 200) { cb(response.status, response.statusText); }
            else {
                response.text()
                .then(function(data) { cb(200, data); })
                .catch(function(err) { cb(-1, err); });
            }
        })
        .catch(function(err) { cb(-1, err); });
}

function setReg(reg, offset, mask, value, cb) {
    //console.log('Set Reg', '0x'+reg.toString(16), offset, '0x'+mask.toString(16), '0x'+value.toString(16), '('+value+')');
    value = (value & mask) << offset;
    mask = mask << offset;
    fetchUrl(`${baseHost}/reg?reg=${reg}&mask=${mask}&val=${value}`, cb);
}

function getReg(reg, offset, mask, cb) {
    mask = mask << offset;
    fetchUrl(`${baseHost}/greg?reg=${reg}&mask=${mask}`, function(code, txt) {
        let value = 0;
        if(code == 200) {
            value = parseInt(txt);
            value = (value & mask) >> offset;
            txt = ''+value;
        }
        cb(code, txt);
    });
}

const updateRegValue = (el, value, updateRemote) => {
    let initialValue;
    let offset = el.attributes.offset ? parseInt(el.attributes.offset.nodeValue) : 0;
    let mask   = (el.attributes.mask ? parseInt(el.attributes.mask.nodeValue) : 255) << offset;
    value = (value & mask) >> offset;
    if (el.type === 'checkbox') {
        initialValue = el.checked;
        value = !!value;
        el.checked = value;
    }
    else
    {
        initialValue = el.value;
        el.value = value;
    }
}

const printReg = (el) => {
    let reg    = el.attributes.reg ? parseInt(el.attributes.reg.nodeValue) : 0;
    let offset = el.attributes.offset ? parseInt(el.attributes.offset.nodeValue) : 0;
    let mask   = el.attributes.mask ? parseInt(el.attributes.mask.nodeValue) : 255;
    let value = 0;
    switch (el.type) {
        case 'checkbox':
            value = el.checked ? mask : 0;
            break;
        case 'range':
        case 'select-one':
            value = el.value;
            break
        default:
            return;
    }
    value = (value & mask) << offset;
    return '0x'+reg.toString(16)+', 0x'+value.toString(16);
}

/* start_x is used to pass native resolution, start_y is not used*/
function setWindow(start_x, offset_x,offset_y, total_x,total_y, output_x, output_y, cb)
{
    fetchUrl(`${baseHost}/resolution?sx=${start_x}&offx=${offset_x}&offy=${offset_y}&tx=${total_x}&ty=${total_y}&ox=${output_x}&oy=${output_y}`, cb);
}

const hide = el => {
    el.classList.add('hidden');
}
const show = el => {
    el.classList.remove('hidden');
}

const disable = el => {
    el.classList.add('disabled');
    el.disabled = true;
}

const enable = el => {
    el.classList.remove('disabled');
    el.disabled = false;
}

const updateGUIvalue = (el, value, updateRemote) => {

    updateRemote = updateRemote == null ? true : updateRemote;

    let initialValue;
    if (el.type === 'checkbox')
    {
        initialValue = el.checked;
        value = !!value;
        el.checked = value;
    }
    else
    {
        initialValue = el.value;
        el.value = value;
    }

    if (updateRemote && initialValue !== value) {
        updateDeviceParam(el);
    }
    else if (!updateRemote)
    {
        if (el.id === "aec") {
            value ? hide(exposure) : show(exposure);
        }
        else if(el.id === "agc")
        {
            if (value) {
                show(gainCeiling);
                hide(agcGain);
            }
            else
            {
                hide(gainCeiling);
                show(agcGain);
            }
        }
        else if (el.id === "awb_gain") {
            value ? show(wb) : hide(wb);
        }
        else if (el.id == "led_intensity") {
            value > -1 ? show(ledGroup) : hide(ledGroup);
        }
    }
}


function updateDeviceParam (el) {

    let value;

    switch (el.type)
    {
        case 'checkbox':
            value = el.checked ? 1 : 0;
            break
        case 'range':
        case 'select-one':
            value = el.value;
            break
        case 'button':
        case 'submit':
            value = '1';
            break
        default:
            return
    }

    const query = `${baseHost}/control?var=${el.id}&val=${value}`;

    fetch(query)
      .then(response => { 
          console.log(`request to ${query} finished, status: ${response.status}`);
      })
}


const ledGroup    = $('led-group');
const agc         = $('agc');
const agcGain     = $('agc_gain-group');
const gainCeiling = $('gainceiling-group');
const aec         = $('aec');
const exposure    = $('aec_value-group');
const awb         = $('awb_gain');
const wb          = $('wb_mode-group');

//=================================================================================

document.addEventListener('DOMContentLoaded', function (event)
{
    const setRegButton = $('set-reg');
    setRegButton.onclick = () => {
        let reg   = parseInt($('reg-addr').value);
        let mask  = parseInt($('reg-mask').value);
        let value = parseInt($('reg-value').value);

        setReg(reg, 0, mask, value, function(code, txt) { if (code != 200) { alert('Error['+code+']: '+txt); }
        });
    }

    const getRegButton = $('get-reg');
    getRegButton.onclick = () => {
        let reg   = parseInt($('get-reg-addr').value);
        let mask  = parseInt($('get-reg-mask').value);
        let value = $('get-reg-value');

        getReg(reg, 0, mask, function(code, txt) {
            if (code != 200) { value.innerHTML = 'Error['+code+']: '+txt; }
            else {
                value.innerHTML = '0x' + parseInt(txt).toString(16) + ' ('+txt+')';
            }
        });
    }

    const setXclkButton = $('set-xclk');
    setXclkButton.onclick = () => {
        let xclk = parseInt($('xclk').value);

        fetchUrl(`${baseHost}/xclk?xclk=${xclk}`,
                 function(code, txt) { if (code != 200) { alert('Error['+code+']: '+txt); }  }
                );
    }

    const mlxAmbReflectedBtn = $('ambReflectedBtn');
    mlxAmbReflectedBtn.onclick = () => {
        let ambRefl = parseInt($('ambReflected').value);

        fetchUrl(`${baseHost}/mlx?ambReflected=${ambRefl}`,
                 function(code, txt) { if (code != 200) { alert('Error['+code+']: '+txt); }  }
                );
    }

    const setResButton = $('set-resolution');
    setResButton.onclick = () => {
        let start_x  = parseInt($('start-x').value);
        let offset_x = parseInt($('offset-x').value);
        let offset_y = parseInt($('offset-y').value);
        let total_x  = parseInt($('total-x').value);
        let total_y  = parseInt($('total-y').value);
        let output_x = parseInt($('output-x').value);
        let output_y = parseInt($('output-y').value);
                  
        setWindow(start_x, offset_x,offset_y, total_x,total_y, output_x,output_y,
                  function (code, txt) {
                      if (code != 200) { alert('Error['+code+']: ' + txt);
                      }
                  });
    }

    const setRegValue = (el) => {

        let reg    = el.attributes.reg    ? parseInt(el.attributes.reg.nodeValue)    : 0;
        let offset = el.attributes.offset ? parseInt(el.attributes.offset.nodeValue) : 0;
        let mask   = el.attributes.mask   ? parseInt(el.attributes.mask.nodeValue)   : 255;
        let value = 0;
        switch (el.type) {
            case 'checkbox':
                value = el.checked ? mask : 0;
                break;
            case 'range':
            case 'text':
            case 'select-one':
                value = el.value;
                break
            default:
                return;
        }

        setReg(reg, offset, mask, value, function(code, txt) { if (code != 200) { alert('Error['+code+']: '+txt); }
        });
    }

    // Attach on change action for register elements
    document
      .querySelectorAll('.reg-action')
      .forEach(el => {
          if (el.type === 'text') {
              el.onkeyup = function(e) { if(e.keyCode == 13) { setRegValue(el); }
                               }
          }
          else { el.onchange = () => setRegValue(el); }
      })


    // read initial values
    fetch(`${baseHost}/status`)
        .then(function (response) { return response.json(); })
        .then(function (state) {
            document
                .querySelectorAll('.default-action')
                    .forEach(el => { updateGUIvalue(el, state[el.id], false); });

            document
                .querySelectorAll('.reg-action')
                    .forEach(el => {
                        let reg = el.attributes.reg ? parseInt(el.attributes.reg.nodeValue) : 0;
                        if (reg == 0) { return; }

                        updateRegValue(el, state['0x' + reg.toString(16)], false);
                    })
        });


    const uploadBtn = $("upload-firmware-btn");
    uploadBtn.addEventListener("click", async () => {
        let fileInput = $("firmware-fileInput");

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

        //const allowedTypes = ["text/html"];
        //if (!allowedTypes.includes(file.type)) {
        //    alert("Invalid file type: " + file.type);
        //    return;
        //}

        if ( !confirm("Are you sure you want to update server firmware?") ) return;

        try {
            // send raw binary directly
            const response = await fetch(`${baseHost}/uploadserver`, {
                method: "POST",
                headers: {
                    "X-Client-Date": new Date().toString().split(' GMT')[0]  // e.g. "Wed, 20 Aug 2025 02:45:32"
                },
                body: file
            });
            if (!response.ok) throw new Error("Upload failed");

            const text = await response.text();

            alert("Server says: " + text);
        }
        catch (err) {
            alert("Error: " + err.message);
        }

    }); // upload


    const rebootBtn = $("reboot-btn");
    rebootBtn.addEventListener("click", () => {

        if ( !confirm("Are you sure you want to reboot esp32 ?") ) return;

        fetch(`${baseHost}/reboot`)
         .then(response => { 
             console.log(`Reboot sent, status: ${response.status}`);
         });

    });


    // Attach default on change action
    document
      .querySelectorAll('.default-action')
      .forEach(el => { el.onchange = () => updateDeviceParam(el); })

    // Custom actions

    // Gain
    agc.onchange = () => {
        updateDeviceParam(agc);
        if (agc.checked)
        {
            show(gainCeiling);
            hide(agcGain);
        }
        else
        {
            hide(gainCeiling);
            show(agcGain);
        }
    }

    // Exposure
    aec.onchange = () => {
        updateDeviceParam(aec);
        aec.checked ? hide(exposure) : show(exposure);
    }

    // AWB
    awb.onchange = () => {
        updateDeviceParam(awb);
        awb.checked ? show(wb) : hide(wb);
    }

})
