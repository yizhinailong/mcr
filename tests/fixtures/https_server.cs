// Local TLS fixture. Generates short-lived certificates without installing any trust-store entries.
using System;
using System.IO;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

public static class McrHttpsFixture
{
    private static X509Certificate2 CreateLeaf(RSA key, X509Certificate2 issuer, bool client)
    {
        var request = new CertificateRequest(client ? "CN=mcr-client" : "CN=localhost", key,
            HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        request.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, true));
        request.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature |
            X509KeyUsageFlags.KeyEncipherment, true));
        var usage = new OidCollection();
        usage.Add(new Oid(client ? "1.3.6.1.5.5.7.3.2" : "1.3.6.1.5.5.7.3.1"));
        request.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(usage, true));
        if (!client)
        {
            var names = new SubjectAlternativeNameBuilder();
            names.AddDnsName("localhost");
            request.CertificateExtensions.Add(names.Build());
        }
        using var publicCertificate = request.Create(issuer, DateTimeOffset.UtcNow.AddMinutes(-2),
            DateTimeOffset.UtcNow.AddHours(12), RandomNumberGenerator.GetBytes(16));
        return publicCertificate.CopyWithPrivateKey(key);
    }

    public static async Task Run(string directory)
    {
        using var rootKey = RSA.Create(2048);
        var rootRequest = new CertificateRequest("CN=mcr-test-root", rootKey, HashAlgorithmName.SHA256,
            RSASignaturePadding.Pkcs1);
        rootRequest.CertificateExtensions.Add(new X509BasicConstraintsExtension(true, false, 0, true));
        rootRequest.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.KeyCertSign, true));
        using var root = rootRequest.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-5), DateTimeOffset.UtcNow.AddDays(1));
        using var serverKey = RSA.Create(2048);
        using var clientKey = RSA.Create(2048);
        using var server = CreateLeaf(serverKey, root, false);
        using var client = CreateLeaf(clientKey, root, true);
        // Schannel requires a normal key container for the server credential. This temporary
        // certificate is never added to a certificate store and is disposed on graceful shutdown.
        Console.Error.WriteLine("TLS fixture: importing server credentials.");
        using var serverCredential = X509CertificateLoader.LoadPkcs12(server.Export(X509ContentType.Pfx, "server-fixture"),
            "server-fixture", X509KeyStorageFlags.DefaultKeySet);
        File.WriteAllText(Path.Combine(directory, "ca.pem"), root.ExportCertificatePem());
        File.WriteAllText(Path.Combine(directory, "client.pem"), client.ExportCertificatePem());
        File.WriteAllText(Path.Combine(directory, "client-key.pem"), clientKey.ExportPkcs8PrivateKeyPem());
        File.WriteAllBytes(Path.Combine(directory, "client.p12"), client.Export(X509ContentType.Pfx, "fixture-password"));
        File.WriteAllText(Path.Combine(directory, "pin.txt"), "sha256//" +
            Convert.ToBase64String(SHA256.HashData(serverKey.ExportSubjectPublicKeyInfo())));
        var plain = new TcpListener(IPAddress.Loopback, 0);
        var mutual = new TcpListener(IPAddress.Loopback, 0);
        plain.Start();
        mutual.Start();
        Console.Error.WriteLine("TLS fixture: HTTPS listeners started.");
        File.WriteAllText(Path.Combine(directory, "ready"),
            ((IPEndPoint)plain.LocalEndpoint).Port + " " + ((IPEndPoint)mutual.LocalEndpoint).Port);
        using var shutdown = new CancellationTokenSource();
        var listeners = Task.WhenAll(Accept(plain, serverCredential, client.Thumbprint, false, shutdown.Token),
            Accept(mutual, serverCredential, client.Thumbprint, true, shutdown.Token));
        while (!File.Exists(Path.Combine(directory, "stop"))) await Task.Delay(25);
        shutdown.Cancel();
        plain.Stop();
        mutual.Stop();
        await listeners;
    }

    private static async Task Accept(TcpListener listener, X509Certificate2 certificate, string clientThumbprint, bool mutual, CancellationToken shutdown)
    {
        try
        {
            while (!shutdown.IsCancellationRequested)
            {
                var client = await listener.AcceptTcpClientAsync(shutdown);
                _ = Handle(client, certificate, clientThumbprint, mutual);
            }
        }
        catch (OperationCanceledException) { }
    }

    private static async Task Handle(TcpClient client, X509Certificate2 certificate, string clientThumbprint, bool mutual)
    {
        using (client)
        using (var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(8)))
        using (var stream = new SslStream(client.GetStream(), false,
            (sender, peer, chain, errors) => !mutual || (peer != null && peer.GetCertHashString() == clientThumbprint)))
        {
            try
            {
                await stream.AuthenticateAsServerAsync(new SslServerAuthenticationOptions
                {
                    ServerCertificate = certificate,
                    ClientCertificateRequired = mutual,
                    EnabledSslProtocols = SslProtocols.Tls12,
                    CertificateRevocationCheckMode = X509RevocationMode.NoCheck
                }, timeout.Token);
                var buffer = new byte[1024];
                var request = new StringBuilder();
                while (!request.ToString().Contains("\r\n\r\n"))
                {
                    int read = await stream.ReadAsync(buffer.AsMemory(), timeout.Token);
                    if (read == 0) return;
                    request.Append(Encoding.ASCII.GetString(buffer, 0, read));
                }
                byte[] response = Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n" +
                    "Connection: close\r\nContent-Type: text/plain\r\n\r\nTLS works");
                await stream.WriteAsync(response.AsMemory(), timeout.Token);
                await stream.FlushAsync(timeout.Token);
            }
            catch (AuthenticationException error) { Console.Error.WriteLine(error.Message); } // Includes expected handshake failures.
            catch (IOException) { }            // Curl may terminate a failed handshake early.
            catch (OperationCanceledException) { }
        }
    }
}
