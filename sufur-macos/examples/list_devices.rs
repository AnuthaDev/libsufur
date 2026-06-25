#[cfg(target_os = "macos")]
use sufur_platform::Platform;

#[cfg(target_os = "macos")]
fn main() {
    let platform = sufur_macos::MacosPlatform;
    match platform.list_devices() {
        Ok(devices) => {
            if devices.is_empty() {
                println!("No removable devices found.");
            }
            for d in &devices {
                println!("Device:");
                println!("  id:          {}", d.id.0);
                println!("  path:        {}", d.path.display());
                println!("  vendor:      {}", d.vendor);
                println!("  model:       {}", d.model);
                println!("  size_bytes:  {}", d.size_bytes);
                println!("  removable:   {}", d.removable);
                println!();
            }
        }
        Err(e) => {
            eprintln!("Error: {e:?}");
            std::process::exit(1);
        }
    }
}

#[cfg(not(target_os = "macos"))]
fn main() {
    eprintln!("this example only runs on macOS");
}
