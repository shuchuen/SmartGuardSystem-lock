R"(
<!DOCTYPE HTML>
<html>
  <head>
    <meta content="text/html; charset=ISO-8859-1" http-equiv="content-type">
    <meta name = "viewport" content = "width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0">
    <title>WiFi Credential Form</title>
    <style>
    body { font-family: Arial, Helvetica, Sans-Serif; Color: #000000; text-align:center; }
    </style>
  </head>
  <body>
    <h3>Enter your WiFi credential</h3>
    <form action="/" method="post">
    <p>
    <label>SSID:&nbsp;</label>
    <input maxlength="30" name="ssid">
    <br>
    <label>Key:&nbsp;&nbsp;&nbsp;</label>
    <input type="password" maxlength="30" name="password">
    <br>
    <!--
    <label>Mode:&nbsp;&nbsp;</label>
    <input type="radio" name="mode" value="Control Hub" checked><label>Control Hub</label>
    <input type="radio" name="mode" value="Standalone" disabled><label>Standalone</label>
    <br>
    -->
    <br>
    <input type="submit" value="Save">
    </p>
    </form>
  </body>
</html>
)"