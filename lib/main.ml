type key = Net of int | Block of int * int | Nothing

external uk_yield: int64 -> bool = "uk_yield"
external uk_netdev_is_queue_ready: int -> bool =
    "uk_netdev_is_queue_ready" [@@noalloc]
external uk_next_io : unit -> key = "uk_next_io"

module Pending_map = Map.Make(
  struct
    (* Net: device_id; Block: device_id * token_id *)
    type t = key
    let compare = compare
  end)

module UkEngine : sig
  val iter : bool -> unit

  val wait_for_work_netdev : int -> unit Lwt.t
  val data_on_netdev : int -> bool
  
  val wait_for_work_blkdev : int -> int -> unit Lwt.t

end  = struct
  let wait_device_ready = ref Pending_map.empty

  let is_in_set set x = not Int.(equal zero (logand set (shift_left one x)))

  let data_on_netdev devid = uk_netdev_is_queue_ready devid

  let iter nonblocking =
    let now = Time.time () in
    let timeout =
      if nonblocking then Int64.zero
      else
        let tm =
          match Time.select_next () with
          | None -> Duration.of_day 1
          | Some tm -> tm
        in
        if tm < now then 0L else Int64.(sub tm now)
    in
    let io = uk_yield timeout in
    if io then
      match uk_next_io () with
      | Nothing -> assert false
      | io -> (
          match Pending_map.find_opt io !wait_device_ready with
          | Some cond -> Lwt_condition.broadcast cond ()
          | _ -> assert false)

  let wait_for_work_netdev devid =
    let key = Net devid in
    match Pending_map.find_opt key !wait_device_ready with
    | None ->
        let cond = Lwt_condition.create () in
        wait_device_ready := Pending_map.add key cond !wait_device_ready;
        Lwt_condition.wait cond
    | Some cond -> Lwt_condition.wait cond

  let wait_for_work_blkdev devid tokid =
    let key = Block (devid, tokid) in
    let cond =
      match Pending_map.find_opt key !wait_device_ready with
      | None ->
          let cond = Lwt_condition.create () in
          wait_device_ready := Pending_map.add key cond !wait_device_ready;
          cond
      | Some cond -> cond
    in
    Lwt_condition.wait cond
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
