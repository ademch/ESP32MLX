

$("upload-firmware-btn").addEventListener("click", async () => {
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

        // Reload the page by adding a timestamp to force fresh load
        window.location.href = window.location.href.split('?')[0] + '?_=' + new Date().getTime();
    }
    catch (err) {
        alert("Error: " + err.message);
    }

}); // upload


$("reboot-btn").addEventListener("click", () => {

    if ( !confirm("Are you sure you want to reboot esp32 ?") ) return;

    fetch(`${baseHost}/reboot`)
     .then(response => { 
         console.log(`Reboot sent, status: ${response.status}`);
     });

});
