external uk_yield: int64 -> int64 = "uk_yield"
external uk_netdev_is_queue_ready: int64 -> bool =
  "uk_netdev_is_queue_ready" [@@noalloc]

module Dev_map = Map.Make(
  struct
    type t = int
    let compare = compare
  end)

module UkEngine : sig
  val iter : bool -> unit
  val wait_for_work_netdev : int -> unit Lwt.t
  val data_on_netdev : int -> bool
end  = struct
  (* TODO wait_writable *)
  let wait_readable = ref Dev_map.empty
    
  let is_in_set set x =
    let i = Int64.(equal zero (logand set (shift_left one x))) in
    i (* FIXME not i?! *)

  let data_on_netdev devid = uk_netdev_is_queue_ready (Int64.of_int devid)

  let iter nonblocking =
    let timeout =
      if nonblocking then Int64.zero
      else
        match Time.select_next () with
        | None -> Int64.add (Time.time ()) (Duration.of_day 1)
        | Some tm -> tm
    in
    let ready_set = uk_yield timeout in
    if not Int64.(equal zero ready_set) then
      Dev_map.iter
        (fun k v ->
          if is_in_set ready_set (k+1) then Lwt_condition.broadcast v ())
        !wait_readable

  let wait_for_work_netdev devid =
    match Dev_map.find_opt devid !wait_readable with
    | None ->
        let cond = Lwt_condition.create () in
        wait_readable := Dev_map.add devid cond !wait_readable;
        Lwt_condition.wait cond
    | Some cond -> Lwt_condition.wait cond
end

(* From lwt/src/unix/lwt_main.ml *)
let rec run t =
  (* Wakeup paused threads now. *)
  Lwt.wakeup_paused ();
  Time.restart_threads Time.time;
  match Lwt.poll t with
  | Some () -> ()
  | None ->
    (* Call enter hooks. *)
    Mirage_runtime.run_enter_iter_hooks ();
    (* Do the main loop call. *)
    UkEngine.iter (Lwt.paused_count () > 0);
    (* Wakeup paused threads again. *)
    Lwt.wakeup_paused ();
    (* Call leave hooks. *)
    Mirage_runtime.run_leave_iter_hooks ();
    run t

(* If the platform doesn't have SIGPIPE, then Sys.set_signal will
   raise an Invalid_argument exception. If the signal does not exist
   then we don't need to ignore it, so it's safe to continue. *)
let ignore_sigpipe () =
  try Sys.(set_signal sigpipe Signal_ignore) with Invalid_argument _ -> ()

(* Main runloop, which registers a callback so it can be invoked
   when timeouts expire. Thus, the program may only call this function
   once and once only. *)
let run t =
  ignore_sigpipe ();
  run t

(* Hopefully we are the first call to [at_exit], since all functions registered
   previously will not be triggered since we are forcing the unikernel shutdown
   here *)
let () =
  at_exit (fun () ->
      Lwt.abandon_wakeups ();
      run (Mirage_runtime.run_exit_hooks ()))
