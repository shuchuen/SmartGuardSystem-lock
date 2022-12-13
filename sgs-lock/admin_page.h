R"(
<!DOCTYPE HTML>
<html>
  <head>
    <meta content="text/html; charset=ISO-8859-1" http-equiv="content-type">
    <meta name = "viewport" content = "width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0">
    <title>Control Page</title>
    <style>
      .container{  
        text-align: center;
        display: grid;
        margin-left: 30%;
        margin-right: 30%;
      }

      .button {
        border: none;
        color: white;
        padding: 15px 32px;
        text-align: center;
        text-decoration: none;
        display: inline-block;
        font-size: 16px;
        margin: 4px 2px;
        cursor: pointer;
      }

      .btn-primary {
        background-color: #4CAF50;
      }

    </style>
  </head>
  <body>
    <div class="container">
      <button class="button btn-primary" onclick='updateStatus(); return false'>UNLOCK</button>
    </div>
  </body>
  <script type="text/javascript">
    function updateStatus() {
      data = {
        "status": "UNLOCK",
      }

      fetch('/status', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(data)
      })
    }

  </script>
</html>
)"