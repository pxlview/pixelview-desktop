// Pixelview regression tests, compiled inside upstream whep_signaller::client.
// Real HTTP on two loopback origins; exercise production POST/parser/PATCH/DELETE.
#[cfg(test)]
mod pixelview_origin_tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpListener;
    use std::sync::{Arc, atomic::{AtomicBool, Ordering}};
    use std::thread;
    use std::time::Duration;

    struct Server {
        url: String,
        requests: Arc<Mutex<Vec<String>>>,
        stop: Arc<AtomicBool>,
        thread: Option<thread::JoinHandle<()>>,
    }
    impl Server {
        fn new(status: u16, location: String) -> Self {
            let listener = TcpListener::bind("127.0.0.1:0").unwrap();
            listener.set_nonblocking(true).unwrap();
            let url = format!("http://{}", listener.local_addr().unwrap());
            let requests = Arc::new(Mutex::new(Vec::new()));
            let log = requests.clone();
            let stop = Arc::new(AtomicBool::new(false));
            let done = stop.clone();
            let thread = thread::spawn(move || {
                while !done.load(Ordering::SeqCst) {
                    match listener.accept() {
                        Ok((mut stream, _)) => {
                            stream.set_read_timeout(Some(Duration::from_secs(2))).unwrap();
                            let mut data = Vec::new();
                            let mut buf = [0; 4096];
                            loop {
                                let n = stream.read(&mut buf).unwrap_or(0);
                                if n == 0 { break; }
                                data.extend_from_slice(&buf[..n]);
                                if data.windows(4).any(|x| x == b"\r\n\r\n") { break; }
                            }
                            log.lock().unwrap().push(String::from_utf8_lossy(&data).lines().next().unwrap_or("").to_string());
                            let body = "v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n";
                            let response = format!("HTTP/1.1 {status} Test\r\nLocation: {location}\r\nContent-Type: application/sdp\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}", body.len());
                            let _ = stream.write_all(response.as_bytes());
                        }
                        Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => thread::sleep(Duration::from_millis(2)),
                        Err(e) => panic!("{e}"),
                    }
                }
            });
            Self { url, requests, stop, thread: Some(thread) }
        }
        fn count(&self) -> usize { self.requests.lock().unwrap().len() }
    }
    impl Drop for Server {
        fn drop(&mut self) {
            self.stop.store(true, Ordering::SeqCst);
            self.thread.take().unwrap().join().unwrap();
        }
    }
    fn setup(endpoint: &str) -> (super::super::WhepClientSignaller, WebRTCSessionDescription, gst::Element) {
        gst::init().unwrap();
        let obj: super::super::WhepClientSignaller = glib::Object::builder().property("whep-endpoint", endpoint).build();
        *obj.imp().state.lock().unwrap() = State::Post { redirects: 0 };
        let sdp = SDPMessage::parse_buffer(b"v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n").unwrap();
        let offer = WebRTCSessionDescription::new(WebRTCSDPType::Offer, sdp);
        (obj, offer, gst::Pipeline::new().upcast())
    }
    #[test]
    fn bad_location_two_origins() {
        for status in [201, 406] {
            let other = Server::new(200, "/ignored".into());
            let origin = Server::new(status, format!("{}/stolen?auth=private", other.url));
            let endpoint = format!("{}/whep?auth=private", origin.url);
            let (obj, offer, bin) = setup(&endpoint);
            RUNTIME.block_on(obj.imp().do_post(offer.clone(), bin, endpoint.parse().unwrap()));
            let state = obj.imp().state.lock().unwrap().clone();
            // Exercise downstream methods if upstream wrongly committed a resource.
            match &state {
                State::Running { whep_resource } => obj.imp().terminate_session(whep_resource),
                State::Patch { .. } => { let _ = RUNTIME.block_on(obj.imp().do_patch(offer)); },
                _ => (),
            }
            assert_eq!(origin.count(), 1);
            assert_eq!(other.count(), 0, "cross-origin session request escaped for {status}");
            assert!(matches!(state, State::Post { .. }), "untrusted resource committed");
        }
    }
    #[test]
    fn post_redirects_never_followed() {
        for status in [301, 302, 303, 307, 308] {
            let other = Server::new(201, "/session".into());
            let origin = Server::new(status, format!("{}/stolen?auth=private", other.url));
            let (obj, offer, bin) = setup(&origin.url);
            RUNTIME.block_on(obj.imp().do_post(offer, bin, origin.url.parse().unwrap()));
            assert_eq!(origin.count(), 1);
            assert_eq!(other.count(), 0, "POST followed {status}");
        }
    }
    #[test]
    fn session_redirects_never_followed() {
        for status in [301, 302, 303, 307, 308] {
            let other = Server::new(200, "/ignored".into());
            let origin = Server::new(status, format!("{}/stolen", other.url));
            let (obj, offer, _) = setup(&origin.url);
            *obj.imp().state.lock().unwrap() = State::Patch { patch: PatchType::AnswerCounterOffer { whep_resource: origin.url.clone() } };
            assert!(RUNTIME.block_on(obj.imp().do_patch(offer)).is_err());
            obj.imp().terminate_session(&origin.url);
            assert_eq!(origin.count(), 2);
            assert_eq!(other.count(), 0, "session followed {status}");
        }
    }
    #[test]
    fn resource_uses_response_origin_not_mutable_setting() {
        let origin = Server::new(201, "/session?resource=token".into());
        let (obj, offer, bin) = setup("https://unrelated.invalid/old");
        RUNTIME.block_on(obj.imp().do_post(offer, bin, origin.url.parse().unwrap()));
        let state = obj.imp().state.lock().unwrap().clone();
        match state {
            State::Running { whep_resource } => assert_eq!(whep_resource, format!("{}/session?resource=token", origin.url)),
            _ => panic!("valid relative resource rejected"),
        }
    }
    #[test]
    fn response_origin_url_matrix() {
        // Parser seam: explicit accepted HTTPS response URL, independent of TLS
        // fixtures. The two-origin tests above cover actual network effects.
        use reqwest::ResponseBuilderExt;
        for (location, accepted) in [
            ("/session?resource=private", true),
            ("https://example.test/session", true),
            ("https://example.test:443/session", true),
            ("//example.test/session", true),
            ("http://example.test:443/session", false),
            ("https://example.test:444/session", false),
            ("https://other.test/session", false),
            ("https://user@example.test/session", false),
            ("https://:pass@example.test/session", false),
            ("//other.test/session", false),
            ("file:///session", false),
        ] {
            let endpoint = "https://example.test/whep?auth=private";
            let (obj, offer, bin) = setup(endpoint);
            let response: reqwest::Response = warp::http::Response::builder()
                .status(201).header("Location", location)
                .url(endpoint.parse().unwrap())
                .body("v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n").unwrap().into();
            RUNTIME.block_on(obj.imp().parse_endpoint_response(offer, response, 0, bin));
            assert_eq!(matches!(*obj.imp().state.lock().unwrap(), State::Running { .. }), accepted, "{location}");
        }
    }
    #[test]
    fn resource_rejects_userinfo_scheme_and_host() {
        for location in ["http://user:pass@127.0.0.1/session", "https://127.0.0.1/session", "http://localhost/session", "file:///session"] {
            let origin = Server::new(201, location.into());
            let (obj, offer, bin) = setup(&origin.url);
            RUNTIME.block_on(obj.imp().do_post(offer, bin, origin.url.parse().unwrap()));
            assert!(matches!(*obj.imp().state.lock().unwrap(), State::Post { .. }), "accepted {location}");
        }
    }
}
