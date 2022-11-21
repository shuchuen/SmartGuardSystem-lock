R"(
<!DOCTYPE HTML>
<html>
  <head>
    <meta content="text/html; charset=ISO-8859-1" http-equiv="content-type">
    <meta name = "viewport" content = "width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0">
    <title>WiFi Credential Form</title>
    <style>
      body {
        font-family: Arial, Helvetica, Sans-Serif;
        Color: #000000
      }

      input[type=text], input[type=password], select {
        width: 100%;
        padding: 12px 20px;
        margin: 8px 0;
        display: inline-block;
        border: 1px solid #ccc;
        border-radius: 4px;
        box-sizing: border-box;
      }

      select {
        cursor: pointer;
      }

      input[type=submit] {
        width: 100%;
        background-color: #007BFF;
        color: white;
        padding: 14px 20px;
        margin: 8px 0;
        border: none;
        border-radius: 4px;
        cursor: pointer;
      }

      .form_container {
        border-radius: 5px;
        background-color: #f2f2f2;
        padding: 20px;
      }

      #standalone_form{
        display:none
      }

    </style>
  </head>
  <body>
    <h3>Enter your Wi-Fi credential and select the function mode</h3>
    <div class="form_container">
      <form action="/" method="post">
        <label for="ssid">SSID</label>
        <input type=text maxlength="30" id="ssid" name="ssid">
        <label for="password">Security Key</label>
        <input type="password" maxlength="30" id="password" name="password">
        <label for="mode">Mode</label>
        <select id="mode" name="mode" onclick='showStandaloneForm()'>
          <option value="PAIRING">Pairing</option>
          <option value="STANDALONE">Standalone</option>
        </select>
        <div id="standalone_form">
          <label for="standaloneUser">Username</label>
          <input type=text maxlength="30" id="standaloneUser" name="standaloneUser">
          <br>
          <label for="standalonePass">Password</label>
          <input type="password" maxlength="30" id="standalonePass" name="standalonePass">
          <br>
        </div>
        <input class="button btn-primary" type="submit" value="Save">
      </form>
    </div>
  </body>
  <script type="text/javascript">
    function showStandaloneForm() {
      var curr = document.getElementById("mode")

      var x = document.getElementById("standalone_form");
      if(curr.value == "STANDALONE"){
        x.style.display = "block";
      } else {
         x.style.display = "none";
      }
    }
  </script>
</html>
)"