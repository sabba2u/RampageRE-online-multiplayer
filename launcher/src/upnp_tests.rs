use super::upnp::*;

#[test]
fn classify_cgnat_and_private() {
    assert!(matches!(classify(None), Reach::HomeOnly { .. }));
    assert!(matches!(classify(Some("100.72.1.5")), Reach::HomeOnly { .. }));
    assert!(matches!(classify(Some("192.168.1.1")), Reach::HomeOnly { .. }));
    assert_eq!(classify(Some("203.0.113.5")), Reach::Internet { public_ip: "203.0.113.5".into() });
}

/// This PC's real router: forwards TCP 19983 for a moment, then removes it.
#[test]
#[ignore]
fn open_and_close_on_this_router() {
    let r = open_lobby_port(19983);
    println!("reach: {r:?}, lan: {:?}", lan_ip());
    close_lobby_port(19983);
}

/// A forward the router made is remembered as made, whatever the address says (double NAT gives a private one), so
/// stopping always removes it.
#[test]
fn a_forward_made_is_known_whatever_the_address() {
    let o = opened(true, Some("10.0.0.1"));
    assert!(o.forwarded && matches!(o.reach, Reach::HomeOnly { .. }));
    let o = opened(true, None);
    assert!(o.forwarded && matches!(o.reach, Reach::HomeOnly { .. }));
    let o = opened(true, Some("203.0.113.5"));
    assert!(o.forwarded && o.reach == Reach::Internet { public_ip: "203.0.113.5".into() });
    assert!(!opened(false, None).forwarded);
}
